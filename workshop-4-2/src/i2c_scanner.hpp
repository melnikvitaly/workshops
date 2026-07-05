#pragma once

#include <cstdint>
#include <cstdio>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================================
//  I2C Scanner — header-only
// ============================================================================
//  Перебирає адреси 1..126 і пінгує кожну (START + адреса + STOP). Якщо
//  пристрій відповів ACK — він присутній на шині. Шину I2C треба
//  ініціалізувати ЗОВНІ (i2c_driver_install) до використання.
// ============================================================================

class I2cScanner {
public:
    explicit I2cScanner(i2c_port_t port) : port_(port) {}

    // Перевірити, чи відповідає пристрій за конкретною адресою
    bool ping(uint8_t address) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(port_, cmd, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        return ret == ESP_OK;
    }

    // Просканувати всю шину, вивести знайдені адреси в лог. Повертає кількість.
    // Якщо передати out/maxOut — знайдені адреси записуються у масив out
    // (не більше maxOut штук), щоб їх можна було показати на екрані.
    int scan(uint8_t *out = nullptr, int maxOut = 0) {
        ESP_LOGI(kTag, "Scanning I2C bus...");
        int found = 0;
        for (uint8_t addr = 1; addr < 127; ++addr) {
            if (ping(addr)) {
                ESP_LOGI(kTag, "Found device at 0x%02x", addr);
                if (out && found < maxOut) {
                    out[found] = addr;
                }
                ++found;
            }
        }
        ESP_LOGI(kTag, "I2C scan complete, %d devices found", found);
        return found;
    }

    // Просканувати шину й одразу сформувати рядок адрес "0x3C 0x68 ..." для
    // показу на екрані (не більше maxShown адрес). Якщо нічого не знайдено —
    // записуємо "0x00". Повертає загальну кількість знайдених пристроїв.
    int scanToString(char *out, size_t outLen, int maxShown = kMaxDevices) {
        if (maxShown > kMaxDevices) {
            maxShown = kMaxDevices;
        }
        uint8_t addrs[kMaxDevices];
        const int found = scan(addrs, maxShown);

        const int shown = (found < maxShown) ? found : maxShown;
        int pos = 0;
        for (int i = 0; i < shown; ++i) {
            pos += snprintf(out + pos, outLen - pos,
                            "%s0x%02X", (i ? " " : ""), addrs[i]);
        }
        if (found == 0) {
            snprintf(out, outLen, "0x00");  // нічого не знайдено
        }
        return found;
    }

private:
    static constexpr const char *kTag = "I2C_SCAN";
    static constexpr int kMaxDevices = 8;  // ємність внутрішнього буфера адрес
    i2c_port_t port_;
};
