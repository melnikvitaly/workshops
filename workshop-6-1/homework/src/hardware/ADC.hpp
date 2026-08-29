#pragma once
#include <cstdlib>
#include <esp_adc/adc_oneshot.h>
#include "Debug.hpp"

// Potentiometer on an ADC1 channel, read through the oneshot driver.
// Hysteresis in raw counts suppresses the jitter of a cheap pot; a second
// threshold in percent decides when the change is worth logging.
class ADC
{
    static constexpr uint16_t HYSTERESIS           = 100;
    static constexpr uint8_t  HYSTERESIS_PERCENTS  = 1;
    static constexpr uint8_t  SAMPLES              = 16;

    adc_channel_t             _channel;
    adc_bitwidth_t            _bitwidth;
    uint8_t                   _treatAsZeroPercents;
    Debug                     dbg;
    adc_oneshot_unit_handle_t _unit         = nullptr;
    int32_t                   _lastValue    = 0;
    uint8_t                   _lastPercents = 0;

public:
    ADC(adc_channel_t channel, adc_bitwidth_t bitwidth, Debug dbg, uint8_t treatAsZeroPercents)
        : _channel(channel), _bitwidth(bitwidth), _treatAsZeroPercents(treatAsZeroPercents), dbg(dbg)
    {
    }

    void init()
    {
        adc_oneshot_unit_init_cfg_t unitCfg = {
            .unit_id  = ADC_UNIT_1,
            .clk_src  = ADC_DIGI_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitCfg, &_unit));

        adc_oneshot_chan_cfg_t chanCfg = {
            .atten    = ADC_ATTEN_DB_12, // full 0..3.3V swing of the pot
            .bitwidth = _bitwidth,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(_unit, _channel, &chanCfg));
    }

    uint8_t percent()
    {
        int32_t sum = 0;
        for (uint8_t i = 0; i < SAMPLES; i++)
        {
            int raw = 0;
            if (adc_oneshot_read(_unit, _channel, &raw) != ESP_OK)
                return _lastPercents;
            sum += raw;
        }
        const int32_t value = sum / SAMPLES;

        if (abs(_lastValue - value) < HYSTERESIS)
            return _lastPercents;
        _lastValue = value;

        const int32_t maxCount = (1L << _bitwidth) - 1;
        uint8_t       newValue = static_cast<uint8_t>(value * 100 / maxCount);

        if (abs(static_cast<int32_t>(_lastPercents) - static_cast<int32_t>(newValue)) >= HYSTERESIS_PERCENTS)
            dbg.print("ADC:%u=>%u", _lastPercents, newValue);

        if (newValue < _treatAsZeroPercents)
            newValue = 0;

        _lastPercents = newValue;
        return _lastPercents;
    }
};
