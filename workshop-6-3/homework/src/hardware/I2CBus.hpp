#pragma once
#include <cstdint>

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_log.h>

#include "Config.hpp"

// The `i2c_master` API of IDF 5+/6 — the legacy driver/i2c.h used in workshop
// 4-2 no longer exists in IDF 6.
class I2CBus
{
    static constexpr const char *TAG = "i2c";

    i2c_master_bus_handle_t _bus = nullptr;

public:
    bool init()
    {
        // Field by field, not designated initializers: the IDF struct has an
        // anonymous union in the middle and its field order is not API.
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port = Config::I2C_PORT;
        cfg.sda_io_num = Config::I2C_SDA_PIN;
        cfg.scl_io_num = Config::I2C_SCL_PIN;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt = Config::I2C_GLITCH_IGNORE_CNT;
        cfg.flags.enable_internal_pullup = true; // ~45k: a fallback, not a substitute
                                                 // for the external 4.7k pull-ups

        const esp_err_t err = i2c_new_master_bus(&cfg, &_bus);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "bus init failed: %s", esp_err_to_name(err));
            return false;
        }
        ESP_LOGI(TAG, "bus up on SDA=%d SCL=%d @ %lu Hz",
                 static_cast<int>(Config::I2C_SDA_PIN),
                 static_cast<int>(Config::I2C_SCL_PIN),
                 static_cast<unsigned long>(Config::I2C_FREQ_HZ));
        return true;
    }

    bool probe(uint16_t address) const
    {
        return i2c_master_probe(_bus, address, Config::I2C_TIMEOUT_MS) == ESP_OK;
    }

    bool addDevice(uint16_t address, i2c_master_dev_handle_t &out) const
    {
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = address;
        cfg.scl_speed_hz = Config::I2C_FREQ_HZ;

        const esp_err_t err = i2c_master_bus_add_device(_bus, &cfg, &out);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "add device 0x%02X failed: %s", static_cast<unsigned>(address), esp_err_to_name(err));
        return err == ESP_OK;
    }

    // Repeated START, so the index write and the read are one transaction.
    static esp_err_t readRegisters(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
    {
        return i2c_master_transmit_receive(dev, &reg, sizeof(reg), buf, len, Config::I2C_TIMEOUT_MS);
    }

    static esp_err_t writeRegister(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
    {
        const uint8_t frame[] = {reg, value};
        return i2c_master_transmit(dev, frame, sizeof(frame), Config::I2C_TIMEOUT_MS);
    }
};
