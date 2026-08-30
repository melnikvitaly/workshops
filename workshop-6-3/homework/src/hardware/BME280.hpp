#pragma once
#include <cstdint>

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Config.hpp"
#include "I2CBus.hpp"

// BME280 over I2C, temperature only. Constants below follow the Bosch datasheet
// (register map §5.3, compensation §4.2.3).
class BME280
{
    static constexpr const char *TAG = "bme280";

#pragma region DATASHEET
    static constexpr uint8_t REG_CHIP_ID = 0xD0;
    static constexpr uint8_t REG_RESET = 0xE0;
    static constexpr uint8_t REG_CALIB_T = 0x88; // dig_T1..dig_T3, 6 bytes LSB-first
    static constexpr uint8_t REG_STATUS = 0xF3;
    static constexpr uint8_t REG_CTRL_MEAS = 0xF4;
    static constexpr uint8_t REG_CONFIG = 0xF5;
    static constexpr uint8_t REG_TEMP_MSB = 0xFA; // MSB, LSB, XLSB — 3 bytes

    static constexpr uint8_t CHIP_ID_BME280 = 0x60;
    static constexpr uint8_t RESET_MAGIC = 0xB6;

    static constexpr uint8_t OSRS_T_X1 = 0x01 << 5;
    static constexpr uint8_t OSRS_P_SKIP = 0x00 << 2;
    static constexpr uint8_t MODE_FORCED = 0x01; // one conversion, then back to sleep
    static constexpr uint8_t CTRL_MEAS_ONE_SHOT = OSRS_T_X1 | OSRS_P_SKIP | MODE_FORCED;
    static constexpr uint8_t CONFIG_NO_FILTER = 0x00; // t_standby irrelevant in forced mode

    static constexpr uint8_t STATUS_MEASURING = 1 << 3;

    static constexpr size_t CALIB_BYTES = 6;
    static constexpr size_t TEMP_BYTES = 3;

    // Raw temperature is 20-bit, left-aligned across the three registers.
    static constexpr uint8_t RAW_MSB_SHIFT = 12;
    static constexpr uint8_t RAW_LSB_SHIFT = 4;
    static constexpr uint8_t RAW_XLSB_SHIFT = 4;

    static constexpr int32_t HUNDREDTHS_PER_DEGREE = 100; // compensation returns 0.01 C
#pragma endregion

#pragma region TIMING
    static constexpr uint32_t RESET_SETTLE_MS = 5;  // datasheet start-up time after reset
    static constexpr uint32_t MEASURE_START_MS = 1; // let the chip actually raise "measuring"
    static constexpr uint32_t MEASURE_POLL_MS = 1;
    static constexpr uint8_t MEASURE_POLL_MAX = 10; // T x1 finishes in ~4 ms; this is the ceiling
#pragma endregion

    const I2CBus &_bus;
    i2c_master_dev_handle_t _dev = nullptr;

    // Factory trim, per chip — the compensation formula is useless without it.
    uint16_t _digT1 = 0;
    int16_t _digT2 = 0;
    int16_t _digT3 = 0;

    bool _ready = false;

public:
    explicit BME280(const I2CBus &bus) : _bus(bus) {}

    // False instead of abort(): a missing sensor costs its own producer, no more.
    bool init()
    {
        // Ping first, so the log separates a wiring problem from a wrong part.
        if (!_bus.probe(Config::BME280_ADDR))
        {
            ESP_LOGE(TAG, "no device at 0x%02X", static_cast<unsigned>(Config::BME280_ADDR));
            return false;
        }
        if (!_bus.addDevice(Config::BME280_ADDR, _dev))
            return false;

        uint8_t chipId = 0;
        if (I2CBus::readRegisters(_dev, REG_CHIP_ID, &chipId, sizeof(chipId)) != ESP_OK)
        {
            ESP_LOGE(TAG, "chip id read failed");
            return false;
        }
        if (chipId != CHIP_ID_BME280)
        {
            // 0x58 here means a BMP280 — same registers, but this driver is
            // written against the BME280 id on purpose.
            ESP_LOGE(TAG, "wrong chip id 0x%02X (expected 0x%02X)",
                     static_cast<unsigned>(chipId),
                     static_cast<unsigned>(CHIP_ID_BME280));
            return false;
        }

        I2CBus::writeRegister(_dev, REG_RESET, RESET_MAGIC);
        vTaskDelay(pdMS_TO_TICKS(RESET_SETTLE_MS));

        if (!readCalibration())
            return false;

        I2CBus::writeRegister(_dev, REG_CONFIG, CONFIG_NO_FILTER);

        ESP_LOGI(TAG, "ready at 0x%02X, dig_T = %u/%d/%d",
                 static_cast<unsigned>(Config::BME280_ADDR),
                 static_cast<unsigned>(_digT1),
                 static_cast<int>(_digT2),
                 static_cast<int>(_digT3));
        _ready = true;
        return true;
    }

    bool ready() const { return _ready; }

    // One forced-mode conversion per call. `out` is left untouched on any bus
    // error — a BOOT press on GPIO9 (= SCL) lands here, and a corrupted transfer
    // must never reach the queue as a reading.
    bool readTemperature(float &out)
    {
        if (!_ready)
            return false;

        if (I2CBus::writeRegister(_dev, REG_CTRL_MEAS, CTRL_MEAS_ONE_SHOT) != ESP_OK)
        {
            ESP_LOGW(TAG, "trigger failed");
            return false;
        }

        if (!waitForConversion())
        {
            ESP_LOGW(TAG, "conversion timeout");
            return false;
        }

        uint8_t raw[TEMP_BYTES] = {};
        if (I2CBus::readRegisters(_dev, REG_TEMP_MSB, raw, sizeof(raw)) != ESP_OK)
        {
            ESP_LOGW(TAG, "sample read failed");
            return false;
        }

        const int32_t adcT = (static_cast<int32_t>(raw[0]) << RAW_MSB_SHIFT) |
                             (static_cast<int32_t>(raw[1]) << RAW_LSB_SHIFT) |
                             (static_cast<int32_t>(raw[2]) >> RAW_XLSB_SHIFT);

        out = static_cast<float>(compensate(adcT)) / HUNDREDTHS_PER_DEGREE;
        return true;
    }

private:
    bool readCalibration()
    {
        uint8_t calib[CALIB_BYTES] = {};
        if (I2CBus::readRegisters(_dev, REG_CALIB_T, calib, sizeof(calib)) != ESP_OK)
        {
            ESP_LOGE(TAG, "calibration read failed");
            return false;
        }
        // LSB first; T1 is unsigned, T2 and T3 are signed.
        _digT1 = static_cast<uint16_t>(calib[0] | (calib[1] << 8));
        _digT2 = static_cast<int16_t>(calib[2] | (calib[3] << 8));
        _digT3 = static_cast<int16_t>(calib[4] | (calib[5] << 8));
        return true;
    }

    // Forced mode clears the "measuring" bit when the sample is ready.
    bool waitForConversion()
    {
        // The bit is not set the instant the trigger write ends, so wait before
        // polling — a "measuring == 0" read could be the previous conversion.
        vTaskDelay(pdMS_TO_TICKS(MEASURE_START_MS));

        for (uint8_t i = 0; i < MEASURE_POLL_MAX; i++)
        {
            uint8_t status = 0;
            if (I2CBus::readRegisters(_dev, REG_STATUS, &status, sizeof(status)) != ESP_OK)
                return false;
            if ((status & STATUS_MEASURING) == 0)
                return true;
            vTaskDelay(pdMS_TO_TICKS(MEASURE_POLL_MS));
        }
        return false;
    }

    // Bosch's fixed-point compensation (BME280_compensate_T_int32): var1 linear,
    // var2 quadratic, result in hundredths of a degree.
    int32_t compensate(int32_t adcT) const
    {
        const int32_t var1 = (((adcT >> 3) - (static_cast<int32_t>(_digT1) << 1)) * static_cast<int32_t>(_digT2)) >> 11;
        const int32_t d = (adcT >> 4) - static_cast<int32_t>(_digT1);
        const int32_t var2 = (((d * d) >> 12) * static_cast<int32_t>(_digT3)) >> 14;
        const int32_t tFine = var1 + var2;
        return (tFine * 5 + 128) >> 8;
    }
};
