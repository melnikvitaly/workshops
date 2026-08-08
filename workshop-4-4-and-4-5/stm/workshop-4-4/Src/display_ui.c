#include "display_ui.h"

#include <stdio.h>

#include "config.h"        // розкладка кадру, періоди, розміри буферів
#include "ssd1306.h"       // OLED дисплей (0x3C)
#include "text_renderer.h" // малює годинник/адреси шрифтом 5x7
#include "i2c_scanner.h"   // сканер шини I2C
#include "ds1307.h"        // RTC годинник реального часу (0x68)
#include "bme280.h"        // T / RH / P (0x76 або 0x77)
#include "i2c_bus.h"       // аварійне відновлення шини
#include "telemetry_link.h"// лічильники SPI-лінка для нижнього рядка
#include "sensor_stream.h" // поточні покази датчиків
#include "soft_timer.h"    // періодичність на HAL_GetTick

// Шина, на якій сидять дисплей, RTC і BME280 (піднімається в main.c).
static I2C_HandleTypeDef *g_hi2c = NULL;

// Об'єкти та стан для дисплея / годинника / сканера
static Ssd1306  g_oled;                 // готовність дисплея зберігає сам драйвер (oled.ready)
static uint32_t g_lastScanMs  = 0;      // коли востаннє сканували шину
static uint32_t g_lastFrameMs = 0;      // коли востаннє оновлювали кадр на екрані

// Скільки разів зривався обмін з дисплеєм по I2C (таймаут/NACK/зайнята шина).
// Показуємо на екрані як "E<n>": нуль, що тримається, означає, що запасу за
// часом вистачає; зростання — що шина на межі. Обмежене згори, щоб рядок
// лічильників не розповзався по ширині.
#define OLED_FAIL_MAX 999u
static uint32_t g_oledFailCount = 0;

// Скільки одиниць в одному кроці десяткового дробу (23.4 -> сота частка / 10).
#define CENTI_PER_UNIT   100
#define CENTI_PER_TENTH  10

// Рядки одного кадру (показуємо на екрані щокадру). Стан I2C-шини показуємо у
// hex — так само, як його друкує сканер. Прочерки — те, що видно до першого
// успішного виміру відповідного датчика, і те, куди рядок повертається, якщо
// датчик зник із шини.
typedef struct {
    char devices[DEVICES_STR_LEN];  // "0x3C 0x50 0x68 0x76" — результат сканування
    char clock[CLOCK_STR_LEN];      // "HH:MM:SS" — час з RTC
    char envTH[ENV_TH_STR_LEN];     // "T:23.4C RH:45%" — з BME280
    char envP[ENV_P_STR_LEN];       // "P:1013 hPa" — з BME280
    char light[LIGHT_STR_LEN];      // "L1003 61%" — сире значення АЦП і відсотки
    char rates[RATES_STR_LEN];      // "TX<кадрів> F<збоїв SPI> E<збоїв I2C>"
} FrameData;

static FrameData g_frame = {
    "0x00", "--:--:--", "T:--.-C RH:--%", "P:---- hPa", "L---- --%", "TX0 F0 E0",
};

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

// Намалювати один кадр: годинник + адреси + T/RH + тиск + освітлення + лінк.
// Розкладку (Y та масштаб кожного рядка) задано в config.h.
static void render_frame(void)
{
    SSD1306_Clear(&g_oled, SSD1306_FILL_BLANK);   // чистимо буфер: далі домальовуємо всі рядки

    Text_DrawTextCentered(&g_oled, CLOCK_Y,   g_frame.clock,   CLOCK_SCALE);    // годинник зверху
    Text_DrawTextCentered(&g_oled, DEVICES_Y, g_frame.devices, DEVICES_SCALE);  // адреси під ним
    Text_DrawTextCentered(&g_oled, ENV_TH_Y,  g_frame.envTH,   ENV_SCALE);      // температура + вологість
    Text_DrawTextCentered(&g_oled, ENV_P_Y,   g_frame.envP,    ENV_SCALE);      // тиск
    Text_DrawTextCentered(&g_oled, LIGHT_Y,   g_frame.light,   LIGHT_SCALE);    // освітлення
    Text_DrawTextCentered(&g_oled, RATES_Y,   g_frame.rates,   RATES_SCALE);    // лічильники SPI-лінка

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

// Зібрати рядок BME280 для температури і вологості.
//
// Знак доводиться складати вручну: при -0.5 °C ціла частина цілочисельного
// ділення -50/100 дорівнює 0, тобто "%ld.%ld" надрукувало б "0.5" — рівно ту
// саму температуру, що й +0.5. Тому мінус приписуємо окремо, коли значення
// від'ємне, а ціла частина вийшла нульовою.
static void format_env(const Bme280Reading *r)
{
    const int32_t centi = r->tempC100;
    const int32_t whole = centi / CENTI_PER_UNIT;
    int32_t       frac  = centi % CENTI_PER_UNIT;
    const char   *sign  = (centi < 0 && whole == 0) ? "-" : "";

    if (frac < 0) {
        frac = -frac;
    }

    snprintf(g_frame.envTH, sizeof(g_frame.envTH), "T:%s%ld.%ldC RH:%u%%",
             sign, (long)whole, (long)(frac / CENTI_PER_TENTH),
             (unsigned)(r->humidity100 / CENTI_PER_UNIT));

    // Паскалі -> гектопаскалі: та сама одиниця, у якій погоду показують усі
    // (1013 hPa = нормальний тиск на рівні моря).
    snprintf(g_frame.envP, sizeof(g_frame.envP), "P:%lu hPa",
             (unsigned long)(r->pressurePa / CENTI_PER_UNIT));
}

// Раз на FRAME_PERIOD_MS оновити рядки з датчиків і перемалювати кадр.
static void poll_frame(void)
{
    if (!SoftTimer_Due(&g_lastFrameMs, FRAME_PERIOD_MS)) {
        return;
    }

    // Датчики опитує sensor_stream у своєму темпі; кадр лише читає останнє
    // опубліковане значення. Рядок лишається без змін, якщо датчик мовчить —
    // показуємо старе значення, а не нулі.
    RtcTime t;
    if (SensorStream_LatestTime(&t)) {
        snprintf(g_frame.clock, sizeof(g_frame.clock), "%02u:%02u:%02u",
                 (unsigned)t.hours, (unsigned)t.minutes, (unsigned)t.seconds);
    }

    Bme280Reading env;
    if (SensorStream_LatestEnv(&env)) {
        format_env(&env);
    }

    // L = сире значення АЦП світла (0..4095, те саме, що йде в SPI-пакет), і
    // поруч воно ж у відсотках — саме відсотки просить завдання передавати.
    snprintf(g_frame.light, sizeof(g_frame.light), "L%u %u%%",
             (unsigned)SensorStream_LatestLightRaw(),
             (unsigned)SensorStream_LatestLightPercent());

    // TX = скільки кадрів телеметрії ця плата вже віддала по SPI,
    // F  = скільки разів HAL_SPI_Transmit повернув помилку/таймаут,
    // E  = скільки разів зривався обмін з дисплеєм по I2C від старту.
    //
    // ⚠️ TX рахує відправлені кадри, а не доставлені: лінк односторонній, тож
    // майстер не знає, чи був раб готовий їх прийняти. Пропуски видно лише на
    // консолі ESP32 — за розривами в номерах кадрів.
    TelemetryLinkStats st;
    TelemetryLink_GetStats(&st);
    snprintf(g_frame.rates, sizeof(g_frame.rates), "TX%lu F%lu E%lu",
             (unsigned long)st.framesSent,
             (unsigned long)st.framesFailed,
             (unsigned long)g_oledFailCount);

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
    poll_frame();      // оновлення кадру: годинник + адреси + T/RH/P + світло + лінк
}
