#pragma once

#include <cstdint>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================================
//  DS1307 RTC driver (I2C) — header-only
// ============================================================================
//  Карта регістрів (усі значення у BCD):
//    0x00  секунди (старший біт = CH, Clock Halt)
//    0x01  хвилини
//    0x02  години
//    0x03  день тижня
//    0x04  число
//    0x05  місяць
//    0x06  рік
//  Шину I2C треба ініціалізувати ЗОВНІ (i2c_driver_install) до використання.
// ============================================================================

struct RtcTime {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;     // день тижня 1..7
    uint8_t date;    // число 1..31
    uint8_t month;   // 1..12
    uint8_t year;    // 0..99 (роки 2000..2099)
};

class Ds1307 {
public:
    Ds1307(i2c_port_t port, uint8_t address = 0x68)
        : port_(port), address_(address) {}

    // Зчитати лише секунди (зручно для тесту/тіку годинника)
    esp_err_t readSeconds(uint8_t &seconds) {
        uint8_t raw = 0;
        esp_err_t ret = readRegisters(0x00, &raw, 1);
        if (ret == ESP_OK) {
            seconds = bcdToDec(raw & 0x7F);  // відкидаємо біт CH (Clock Halt)
        }
        return ret;
    }

    // Зчитати повний час
    esp_err_t readTime(RtcTime &out) {
        uint8_t raw[7] = {0};
        esp_err_t ret = readRegisters(0x00, raw, sizeof(raw));
        if (ret != ESP_OK) {
            return ret;
        }
        out.seconds = bcdToDec(raw[0] & 0x7F);
        out.minutes = bcdToDec(raw[1] & 0x7F);
        out.hours   = bcdToDec(raw[2] & 0x3F);  // 24-годинний формат
        out.day     = bcdToDec(raw[3] & 0x07);
        out.date    = bcdToDec(raw[4] & 0x3F);
        out.month   = bcdToDec(raw[5] & 0x1F);
        out.year    = bcdToDec(raw[6]);
        return ret;
    }

    // Записати повний час (запускає годинник, скидаючи біт CH)
    esp_err_t setTime(const RtcTime &t) {
        const uint8_t raw[7] = {
            decToBcd(t.seconds & 0x7F),  // CH=0 -> годинник запущено
            decToBcd(t.minutes),
            decToBcd(t.hours),           // bit6=0 -> 24-годинний формат
            decToBcd(t.day),
            decToBcd(t.date),
            decToBcd(t.month),
            decToBcd(t.year),
        };
        return writeRegisters(0x00, raw, sizeof(raw));
    }

private:
    static uint8_t bcdToDec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
    static uint8_t decToBcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

    esp_err_t readRegisters(uint8_t reg, uint8_t *data, size_t len) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        // Записуємо номер стартового регістра
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        // Restart і читаємо len байтів
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_READ, true);
        if (len > 1) {
            i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);  // останній = NACK
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(port_, cmd, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        if (ret != ESP_OK) {
            ESP_LOGE(kTag, "read reg 0x%02x failed: %s", reg, esp_err_to_name(ret));
        }
        return ret;
    }

    esp_err_t writeRegisters(uint8_t reg, const uint8_t *data, size_t len) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_write(cmd, const_cast<uint8_t *>(data), len, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(port_, cmd, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        if (ret != ESP_OK) {
            ESP_LOGE(kTag, "write reg 0x%02x failed: %s", reg, esp_err_to_name(ret));
        }
        return ret;
    }

    static constexpr const char *kTag = "DS1307";

    i2c_port_t port_;
    uint8_t    address_;
};
