#pragma once
#include <esp_adc/adc_oneshot.h>
#include <hardware/Debug.h>
#include <cstdlib>
#include <string>

class ADC
{
private:
    // On ESP32-C3, GPIO 0-4 map directly to ADC1 channels 0-4
    adc_channel_t _channel;
    adc_oneshot_unit_handle_t _handle = nullptr;
    uint16_t _resolution;
    uint8_t _treatAsZeroPercents;

    static constexpr uint8_t HYSTERESIS = 100;
    static constexpr uint8_t HYSTERESIS_PERCENTS = 1;

public:
    uint8_t _lastPercents = 0;
    uint16_t _lastValue = 0;
    Debug &dbg;

    ADC(uint8_t pin, uint16_t resolution, Debug &dbg, uint8_t treatAsZeroPercents)
        : _channel((adc_channel_t)pin), _resolution(resolution),
          dbg(dbg), _treatAsZeroPercents(treatAsZeroPercents)
    {
    }

    void init()
    {
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
        };
        adc_oneshot_new_unit(&init_config, &_handle);

        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = (adc_bitwidth_t)_resolution,
        };
        adc_oneshot_config_channel(_handle, _channel, &chan_cfg);
    }

    uint8_t percent()
    {
        static constexpr uint8_t SAMPLES = 16;
        int32_t sum = 0;
        for (uint8_t i = 0; i < SAMPLES; i++)
        {
            int raw = 0;
            adc_oneshot_read(_handle, _channel, &raw);
            sum += raw;
        }
        auto value = (int)(sum / SAMPLES);

        if (abs(_lastValue - value) < HYSTERESIS)
            return _lastPercents;

        _lastValue = (uint16_t)value;
        uint32_t maxVal = (1u << _resolution) - 1;
        auto newValue = (uint8_t)((float)value / (float)maxVal * 100.0f);

        if (abs((int)_lastPercents - (int)newValue) >= HYSTERESIS_PERCENTS)
            dbg.print(std::string("ADC:") + std::to_string(_lastPercents) + "=>" + std::to_string(newValue));

        if (newValue < _treatAsZeroPercents)
            newValue = 0;

        _lastPercents = newValue;
        return _lastPercents;
    }
};
