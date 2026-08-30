#pragma once
#include <cstdint>

#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

#include "Config.hpp"

// LDR on an ADC1 channel. The percentage is a position inside the calibrated
// [LDR_RAW_MIN, LDR_RAW_MAX] window, not a fraction of the full 0..4095 span.
class LDR
{
    static constexpr const char *TAG = "ldr";
    static constexpr int32_t PERCENT_FULL = 100;

    adc_oneshot_unit_handle_t _unit = nullptr;

public:
    bool init()
    {
        adc_oneshot_unit_init_cfg_t unitCfg = {};
        unitCfg.unit_id = ADC_UNIT_1; // ADC2 is unusable while Wi-Fi runs
        unitCfg.clk_src = ADC_DIGI_CLK_SRC_DEFAULT;
        unitCfg.ulp_mode = ADC_ULP_MODE_DISABLE;
        if (adc_oneshot_new_unit(&unitCfg, &_unit) != ESP_OK)
        {
            ESP_LOGE(TAG, "unit init failed");
            return false;
        }

        adc_oneshot_chan_cfg_t chanCfg = {};
        chanCfg.atten = Config::LDR_ATTEN;
        chanCfg.bitwidth = Config::LDR_BITWIDTH;
        if (adc_oneshot_config_channel(_unit, Config::LDR_CHANNEL, &chanCfg) != ESP_OK)
        {
            ESP_LOGE(TAG, "channel config failed");
            return false;
        }

        ESP_LOGI(TAG, "ready on ADC1 channel %d, window %ld..%ld",
                 static_cast<int>(Config::LDR_CHANNEL),
                 static_cast<long>(Config::LDR_RAW_MIN),
                 static_cast<long>(Config::LDR_RAW_MAX));
        return true;
    }

    // False on a failed conversion, so the task skips the sample rather than
    // publishing a 0 that looks like darkness.
    bool readPercent(uint8_t &out)
    {
        int32_t sum = 0;
        for (uint8_t i = 0; i < Config::LDR_SAMPLES; i++)
        {
            int raw = 0; // the IDF API insists on a plain int here
            if (adc_oneshot_read(_unit, Config::LDR_CHANNEL, &raw) != ESP_OK)
                return false;
            sum += raw;
        }
        // The C3 ADC swings tens of counts even on a steady input.
        int32_t value = sum / Config::LDR_SAMPLES;

        // Clamp, or a reading outside the window maps below 0 % / above 100 %.
        if (value < Config::LDR_RAW_MIN)
            value = Config::LDR_RAW_MIN;
        if (value > Config::LDR_RAW_MAX)
            value = Config::LDR_RAW_MAX;

        const int32_t span = Config::LDR_RAW_MAX - Config::LDR_RAW_MIN;
        out = static_cast<uint8_t>((value - Config::LDR_RAW_MIN) * PERCENT_FULL / span);
        return true;
    }
};
