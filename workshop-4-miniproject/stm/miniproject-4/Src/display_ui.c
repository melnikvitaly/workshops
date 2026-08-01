#include "display_ui.h"

#include <stdio.h>

#include "config.h"        // розкладка кадру, періоди, розміри буферів
#include "ssd1306.h"       // OLED дисплей (0x3C)
#include "text_renderer.h" // малює годинник/адреси шрифтом 5x7
#include "i2c_scanner.h"   // сканер шини I2C
#include "ds1307.h"        // RTC годинник реального часу (0x68)
#include "i2c_bus.h"       // аварійне відновлення шини
#include "log_emission.h"  // темпи SPI-потоку для нижнього рядка
#include "sensor_stream.h" // поточне значення освітлення
#include "soft_timer.h"    // періодичність на HAL_GetTick

// Шина, на якій сидять дисплей і RTC (піднімається в main.c).
static I2C_HandleTypeDef *g_hi2c = NULL;

// Об'єкти та стан для дисплея / годинника / сканера
static Ssd1306  g_oled;                 // готовність дисплея зберігає сам драйвер (oled.ready)
static uint32_t g_lastScanMs  = 0;      // коли востаннє сканували шину
static uint32_t g_lastFrameMs = 0;      // коли востаннє оновлювали кадр на екрані

// Скільки разів зривався обмін з дисплеєм по I2C (таймаут/NACK/зайнята шина).
// Показуємо на екрані як "E<n>": нуль, що тримається, означає, що запасу за
// часом вистачає; зростання — що шина на межі. Обмежене згори, щоб рядок
// лічильників не розповзався по ширині.
#define OLED_FAIL_MAX 9999u
static uint32_t g_oledFailCount = 0;

// Переведення байтів/с у кілобіти/с для нижнього рядка: множимо на біти в байті
// і ділимо на біти в кілобіті, додаючи півкілобіта для округлення до найближчого.
#define BITS_PER_BYTE      8u
#define BITS_PER_KILOBIT   1000u
#define KILOBIT_ROUNDING   (BITS_PER_KILOBIT / 2u)

// Рядки одного кадру (показуємо на екрані щокадру). Стан I2C-шини показуємо у
// hex — так само, як його друкує сканер.
typedef struct {
    char devices[DEVICES_STR_LEN];  // "0x3C 0x68 ..." — результат сканування
    char clock[CLOCK_STR_LEN];      // "HH:MM:SS" — час з RTC
    char light[LIGHT_STR_LEN];      // "L<АЦП>" — сире значення освітлення
    char rates[RATES_STR_LEN];      // "PP<пакетів/с> E<збоїв I2C> <кбіт/с>kb"
} FrameData;

static FrameData g_frame = { "0x00", "--:--:--", "L----", "PP- E0 -kb" };  // до першого виміру

// --------------------------------------------------------------------------
// Init.
// --------------------------------------------------------------------------
void DisplayUI_Init(I2C_HandleTypeDef *hi2c)
{
    g_hi2c = hi2c;

    // Ініціалізація OLED-дисплея на шині I2C (параметри — у config.h).
    // Готовність дисплея далі зберігає сам драйвер (oled.ready).
    SSD1306_Setup(&g_oled, hi2c, OLED_ADDR, OLED_WIDTH, OLED_HEIGHT);
    SSD1306_Init(&g_oled);
}

// --------------------------------------------------------------------------
// Внутрішні помічники кадру.
// --------------------------------------------------------------------------

// Просканувати шину не частіше, ніж раз на SCAN_PERIOD_MS, і оновити
// рядок frame.devices (перший скан спрацьовує одразу).
static void poll_i2c_scan(void)
{
    if (!SoftTimer_Due(&g_lastScanMs, SCAN_PERIOD_MS)) {
        return;
    }
    I2CScanner_ScanToString(g_hi2c, g_frame.devices, sizeof(g_frame.devices), MAX_DEVICES_SHOWN);
}

// Дисплей не піднявся на старті або обмін зірвався — пробуємо раз на кадр,
// доки не вийде. Без цього oled.ready лишався б 0 назавжди. Якщо і сама шина
// не піднялась (I2CBus_Recover повернув не HAL_OK), дисплей чіпати немає сенсу:
// наступний кадр спробує ще раз.
static void recover_display(void)
{
    if (I2CBus_Recover(g_hi2c) != HAL_OK) {
        return;
    }
    SSD1306_Init(&g_oled);   // сам виставить oled.ready за успіху
}

// Намалювати один кадр: годинник + адреси пристроїв + освітлення + темпи SPI.
// Розкладку (Y та масштаб кожного рядка) задано в config.h.
static void render_frame(void)
{
    SSD1306_Clear(&g_oled, SSD1306_FILL_BLANK);   // чистимо буфер: далі домальовуємо всі рядки

    Text_DrawTextCentered(&g_oled, CLOCK_Y, g_frame.clock, CLOCK_SCALE);        // годинник зверху
    Text_DrawTextCentered(&g_oled, DEVICES_Y, g_frame.devices, DEVICES_SCALE);  // адреси під ним
    Text_DrawTextCentered(&g_oled, LIGHT_Y, g_frame.light, LIGHT_SCALE);        // освітлення (АЦП)
    Text_DrawTextCentered(&g_oled, RATES_Y, g_frame.rates, RATES_SCALE);        // темпи SPI-потоку

    // Раніше результат ігнорувався: один зірваний обмін — і картинка застигала
    // без жодних ознак. Тепер помічаємо дисплей як неготовий, і наступний кадр
    // підніме шину (див. poll_frame).
    if (SSD1306_Flush(&g_oled) != HAL_OK) {
        g_oled.ready = 0;
        if (g_oledFailCount < OLED_FAIL_MAX) {
            ++g_oledFailCount;
        }
    }
}

// Раз на FRAME_PERIOD_MS оновити час з RTC і перемалювати кадр.
static void poll_frame(void)
{
    if (!SoftTimer_Due(&g_lastFrameMs, FRAME_PERIOD_MS)) {
        return;
    }

    // frame.clock лишається без змін, якщо RTC не відповів (показуємо старий час).
    DS1307_ReadTimeString(g_hi2c, g_frame.clock, sizeof(g_frame.clock));

    // L = сире значення АЦП світла (0..4095, те саме, що йде в пакет "LGHT").
    snprintf(g_frame.light, sizeof(g_frame.light), "L%u",
             (unsigned)SensorStream_LatestLightRaw());

    // PP (ProducedPackets) = скільки пакетів ЦЯ плата поклала в потік за останню
    //      секунду. Це темп виробника (sensor_stream), а не обміну: він лишається
    //      ненульовим і без майстра — записи просто нікуди не вичитуються.
    // E  = скільки разів зривався обмін з дисплеєм по I2C від старту,
    // <n>kb = темп SPI у кілобітах/с — уже бік споживача: рахується за обертами
    //      TX DMA, а їх крутить тактовий сигнал майстра. Немає майстра — тут нуль.
    //
    // Кілобіти зручні тим, що число прямо звіряється з тактовою частотою SPI:
    // на 1 МГц майстер вичитує ~999kb, тож видно, що лінк іде на повній швидкості.
    // Крок дискретизації — один оберт кільця (2048 Б = ~16 кбіт/с), бо байти
    // рахуються обертами DMA (див. stream_bytes_total у log_emission.c).
    LogEmitStats st;
    LogEmission_GetStats(&st);
    unsigned kbits = (unsigned)((st.bytesPerSec * BITS_PER_BYTE + KILOBIT_ROUNDING)
                                / BITS_PER_KILOBIT);
    snprintf(g_frame.rates, sizeof(g_frame.rates), "PP%u E%u %ukb",
             (unsigned)st.producedPacketsPerSec,
             (unsigned)g_oledFailCount,
             kbits);

    if (g_oled.ready) {
        render_frame();
    } else {
        recover_display();
    }
}

// --------------------------------------------------------------------------
// Poll (головний цикл).
// --------------------------------------------------------------------------
void DisplayUI_Poll(void)
{
    if (g_hi2c == NULL) {
        return;   // DisplayUI_Init() ще не викликали
    }
    poll_i2c_scan();   // сканування шини I2C (кожні 10 с)
    poll_frame();      // оновлення кадру: годинник + адреси + світло + темпи
}
