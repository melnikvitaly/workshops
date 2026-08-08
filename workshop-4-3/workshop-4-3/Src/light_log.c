#include "light_log.h"

#include "config.h"      // LOG_INTERVAL_MS, LIGHT_SAMPLE_COUNT
#include "console.h"     // вивід у монітор порту
#include "ds1307.h"      // мітка часу з RTC по I2C
#include "eeprom.h"      // сам журнал
#include "soft_timer.h"  // періодичність на HAL_GetTick

// АЦП піднімає CubeMX у main.c: один канал (PA0), запуск програмний.
extern ADC_HandleTypeDef hadc1;

// Шина, на якій сидять і RTC (0x68), і EEPROM (0x50) — піднімається в main.c.
static I2C_HandleTypeDef *g_hi2c = NULL;

static uint32_t g_lastLogMs   = 0;
static uint16_t g_nextAddress = EEPROM_DATA_START;

// Розкладка запису в EEPROM (8 байтів, див. light_log.h).
#define REC_MONTH     0
#define REC_DATE      1
#define REC_YEAR      2
#define REC_HOURS     3
#define REC_MINUTES   4
#define REC_SECONDS   5
#define REC_LIGHT_HI  6
#define REC_LIGHT_LO  7

#define LIGHT_HI_SHIFT 8      /* старший байт 16-бітного значення */
#define BYTE_MASK      0xFF
#define PERCENT_FULL   100u
#define MS_PER_SECOND  1000u

// --------------------------------------------------------------------------
// Освітлення: середнє з кількох вибірок АЦП.
// --------------------------------------------------------------------------
uint16_t LightLog_ReadLightRaw(void)
{
    uint32_t sum = 0;
    uint32_t taken = 0;

    for (uint32_t i = 0; i < LIGHT_SAMPLE_COUNT; ++i) {
        if (HAL_ADC_Start(&hadc1) != HAL_OK) {
            break;
        }
        // Одноразове перетворення: запустили — дочекались — забрали. DMA і
        // таймер тут не потрібні, бо міряємо раз на кілька секунд, а не потоком.
        if (HAL_ADC_PollForConversion(&hadc1, LIGHT_ADC_TIMEOUT_MS) == HAL_OK) {
            sum += HAL_ADC_GetValue(&hadc1);
            taken++;
        }
        HAL_ADC_Stop(&hadc1);
    }

    if (taken == 0) {
        return 0;   // АЦП не відповів — краще явний нуль, ніж ділення на нуль
    }
    return (uint16_t)(sum / taken);
}

uint8_t LightLog_Percent(uint16_t raw)
{
    return (uint8_t)(((uint32_t)raw * PERCENT_FULL) / LIGHT_ADC_MAX_RAW);
}

uint16_t LightLog_Count(void)
{
    return (uint16_t)((g_nextAddress - EEPROM_DATA_START) / EEPROM_RECORD_SIZE);
}

// --------------------------------------------------------------------------
// Init.
// --------------------------------------------------------------------------
void LightLog_Init(I2C_HandleTypeDef *hi2c)
{
    g_hi2c        = hi2c;
    g_lastLogMs   = 0;
    g_nextAddress = EEPROM_GetNextAddress(hi2c);
}

// --------------------------------------------------------------------------
// Poll: раз на LOG_INTERVAL_MS дописати запис.
// --------------------------------------------------------------------------
void LightLog_Poll(void)
{
    if (g_hi2c == NULL) {
        return;   // LightLog_Init() ще не викликали
    }
    if (!SoftTimer_Due(&g_lastLogMs, LOG_INTERVAL_MS)) {
        return;
    }
    if (g_nextAddress + EEPROM_RECORD_SIZE > EEPROM_DATA_LIMIT) {
        return;   // журнал заповнено; чистити його — рішення користувача ("clear")
    }

    RtcTime t = {0};
    if (DS1307_ReadTime(g_hi2c, &t) != HAL_OK) {
        Console_Printf("RTC: read failed, record skipped");
        return;   // без часу запис не має сенсу — саме мітка часу тут головна
    }

    uint16_t raw = LightLog_ReadLightRaw();

    uint8_t rec[EEPROM_RECORD_SIZE];
    rec[REC_MONTH]    = t.month;
    rec[REC_DATE]     = t.date;
    rec[REC_YEAR]     = t.year;
    rec[REC_HOURS]    = t.hours;
    rec[REC_MINUTES]  = t.minutes;
    rec[REC_SECONDS]  = t.seconds;
    rec[REC_LIGHT_HI] = (uint8_t)((raw >> LIGHT_HI_SHIFT) & BYTE_MASK);
    rec[REC_LIGHT_LO] = (uint8_t)(raw & BYTE_MASK);

    // Вказівник просуваємо тільки після справді вдалого запису: якщо просто
    // ігнорувати статус, зірваний обмін (NACK/таймаут) мовчки лишає
    // непрописані байти, а вказівник вже стоїть за ними — журнал назавжди має
    // дірку. Провал тут просто повторить спробу на ту саму адресу наступного разу.
    if (EEPROM_Write(g_hi2c, g_nextAddress, rec, sizeof(rec)) != HAL_OK) {
        Console_Printf("EEPROM: write at 0x%04X failed, record skipped", g_nextAddress);
        return;
    }

    uint16_t index = LightLog_Count();
    g_nextAddress += EEPROM_RECORD_SIZE;
    EEPROM_SaveNextAddress(g_hi2c, g_nextAddress);

    char stamp[DS1307_STAMP_LEN];
    DS1307_FormatStamp(&t, stamp, sizeof(stamp));
    Console_Printf("log #%u  %s  light=%u (%u%%)",
                   (unsigned)index, stamp, (unsigned)raw, (unsigned)LightLog_Percent(raw));
}

// --------------------------------------------------------------------------
// Dump: прочитати журнал з EEPROM і вивести в монітор порту.
// --------------------------------------------------------------------------
void LightLog_Dump(void)
{
    if (g_hi2c == NULL) {
        return;
    }

    uint16_t count = LightLog_Count();
    Console_Printf("== EEPROM log: %u of %u records ==", (unsigned)count,
                   (unsigned)EEPROM_MAX_RECORDS);
    if (count == 0) {
        Console_Printf("   (empty — wait %u s for the first record)",
                       (unsigned)(LOG_INTERVAL_MS / MS_PER_SECOND));
        return;
    }
    Console_Printf("  idx  addr    mm:dd:yy hh:mm:ss  raw     %%");

    for (uint16_t i = 0; i < count; ++i) {
        uint16_t addr = (uint16_t)(EEPROM_DATA_START + i * EEPROM_RECORD_SIZE);
        uint8_t rec[EEPROM_RECORD_SIZE];

        if (EEPROM_Read(g_hi2c, addr, rec, sizeof(rec)) != HAL_OK) {
            Console_Printf("  %3u  0x%04X  <read failed>", (unsigned)i, addr);
            continue;
        }

        RtcTime t = {
            .month   = rec[REC_MONTH],
            .date    = rec[REC_DATE],
            .year    = rec[REC_YEAR],
            .hours   = rec[REC_HOURS],
            .minutes = rec[REC_MINUTES],
            .seconds = rec[REC_SECONDS],
        };
        uint16_t raw = (uint16_t)(((uint16_t)rec[REC_LIGHT_HI] << LIGHT_HI_SHIFT) | rec[REC_LIGHT_LO]);

        char stamp[DS1307_STAMP_LEN];
        DS1307_FormatStamp(&t, stamp, sizeof(stamp));
        Console_Printf("  %3u  0x%04X  %s  %4u  %3u",
                       (unsigned)i, addr, stamp, (unsigned)raw,
                       (unsigned)LightLog_Percent(raw));
    }
}

// --------------------------------------------------------------------------
// Clear: почати журнал спочатку.
// --------------------------------------------------------------------------
void LightLog_Clear(void)
{
    if (g_hi2c == NULL) {
        return;
    }
    // Самі байти не стираємо: EEPROM має обмежений ресурс перезаписів, а старі
    // записи все одно перекриються наступними. Досить відкотити вказівник.
    if (EEPROM_SaveNextAddress(g_hi2c, EEPROM_DATA_START) == HAL_OK) {
        g_nextAddress = EEPROM_DATA_START;
        Console_Printf("log cleared");
    } else {
        Console_Printf("EEPROM: pointer reset failed");
    }
}
