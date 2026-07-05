#pragma once

#include <cstddef>
#include <cstdint>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"

// ============================================================================
//  Config — усі налаштування прошивки в одному місці
// ============================================================================
//  Тут зібрано те, що зазвичай хочеться підкрутити: піни, адреси, розкладку
//  екрана, періоди. Логіка лишається в *.hpp драйверах та в main.cpp.
// ============================================================================

namespace config {

// --- Піни та шина I2C (для ESP32-S3 можна обирати майже будь-які вільні GPIO) ---
constexpr int        I2C_MASTER_SCL_IO  = 3;
constexpr int        I2C_MASTER_SDA_IO  = 4;
constexpr i2c_port_t I2C_MASTER_NUM     = I2C_NUM_0;  // Номер порту I2C
constexpr uint32_t   I2C_MASTER_FREQ_HZ = 400000;     // 400 кГц (швидше для OLED)

// --- Параметри OLED-дисплея ---
constexpr uint8_t OLED_ADDR   = 0x3C;  // адреса SSD1306 на шині I2C
constexpr uint8_t OLED_WIDTH  = 128;
constexpr uint8_t OLED_HEIGHT = 64;

// --- Розкладка кадру ---
constexpr int16_t CAT_CENTER_Y  = 46;  // кіт нижче, зверху годинник + адреси
constexpr int16_t CAT_RADIUS    = 14;
constexpr int16_t EAR_TILT_MAX  = 5;   // амплітуда анімації вух
constexpr int16_t CLOCK_Y       = 0;   // годинник у верхньому рядку
constexpr int16_t DEVICES_Y     = 16;  // адреси під годинником
constexpr uint8_t CLOCK_SCALE   = 2;   // 10x14 пікселів на символ
constexpr uint8_t DEVICES_SCALE = 1;   // дрібний шрифт для адрес

// --- Сканування шини I2C ---
constexpr int64_t SCAN_PERIOD_US    = 10 * 1000000LL;  // 10 секунд
constexpr int     MAX_DEVICES_SHOWN = 4;  // скільки адрес влазить на екран

// --- Розміри текстових буферів ---
constexpr size_t DEVICES_STR_LEN = 32;
constexpr size_t CLOCK_STR_LEN   = 16;

// --- Період головного циклу ---
constexpr TickType_t LOOP_PERIOD_MS = 1000;

}  // namespace config
