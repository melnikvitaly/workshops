#include "light_archive.h"

#include "config.h"         // LOG_INTERVAL_MS
#include "eeprom.h"         // журнал у зовнішній EEPROM (0x50)
#include "sensor_stream.h"  // останнє усереднене значення освітлення
#include "soft_timer.h"     // періодичність на HAL_GetTick

// Шина, на якій сидить EEPROM (піднімається в main.c).
static I2C_HandleTypeDef *g_hi2c = NULL;

// Змінні для керування логуванням.
static uint32_t g_lastLogMs   = 0;
static uint16_t g_nextAddress = 0;

void LightArchive_Init(I2C_HandleTypeDef *hi2c)
{
    g_hi2c        = hi2c;
    g_lastLogMs   = 0;
    // Зчитуємо адресу з пам'яті під час старту пристрою.
    g_nextAddress = EEPROM_GetNextAddress(hi2c);
}

// Раз на LOG_INTERVAL_MS виміряти освітлення й дописати його в EEPROM.
// За реальним часом (HAL_GetTick), а не за лічильником ітерацій.
void LightArchive_Poll(void)
{
    if (g_hi2c == NULL) {
        return;   // LightArchive_Init() ще не викликали
    }
    if (!SoftTimer_Due(&g_lastLogMs, LOG_INTERVAL_MS)) {
        return;
    }

    // Потік у SPI (записи "LGHT"/"TIME") веде sensor_stream; тут лише архівуємо
    // останнє усереднене значення освітлення в EEPROM раз на LOG_INTERVAL_MS.
    uint8_t light_percent = SensorStream_LatestLightPercent();
    if (g_nextAddress <= EEPROM_DATA_END) {
        // Просуваємо (і зберігаємо) вказівник лише якщо байт справді записався:
        // якщо просто ігнорувати статус, зірваний обмін (NACK/таймаут) мовчки
        // лишає непрописаний байт, а вказівник вже стоїть за ним — журнал
        // назавжди має дірку. Провал тут просто повторить спробу на цю саму
        // адресу наступного разу.
        if (EEPROM_WriteByte(g_hi2c, g_nextAddress, light_percent) == HAL_OK) {
            g_nextAddress++;                                  // зсуваємо вказівник
            EEPROM_SaveNextAddress(g_hi2c, g_nextAddress);    // і зберігаємо його
        }
    }
}
