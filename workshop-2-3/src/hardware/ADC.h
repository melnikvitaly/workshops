#pragma once
#include <Arduino.h>
#include <hardware/Debug.h>

class ADC
{
private:
    uint8_t _pin;
    static constexpr uint8_t HYSTERESIS = 100;
    static constexpr uint8_t HYSTERESIS_PERCENTS = 1;

public:
    uint8_t _lastPercents;
    uint16_t _lastValue;
    u_int16_t _resolution;
    uint8_t _treatAsZeroPercents;
    Debug &dbg;
    ADC(uint8_t pin, u_int16_t resolution, Debug &dbg, uint8_t treatAsZeroPercents) : _pin(pin), _resolution(resolution), dbg(dbg),
                                                                                      _treatAsZeroPercents(treatAsZeroPercents)
    {
    }

    void init()
    {
        pinMode(_pin, INPUT);
        analogReadResolution(_resolution);
    }

    uint8_t percent()
    {
        static constexpr uint8_t SAMPLES = 16;
        int32_t sum = 0;
        for (uint8_t i = 0; i < SAMPLES; i++)
            sum += analogRead(_pin);
        auto value = (int)(sum / SAMPLES);

        if (abs(_lastValue - value) < HYSTERESIS)
        {
            return _lastPercents;
        }
        _lastValue = value;
        auto newValue = (uint8_t)((float)value / ((1u << _resolution) - 1) * 100);

        if (abs(_lastPercents - newValue) >= HYSTERESIS_PERCENTS)
        {
            dbg.print(String("ADC:") + String(_lastPercents) + "=>" + newValue);
        }
        if (newValue < _treatAsZeroPercents)
        {
            newValue = 0;
        }
        _lastPercents = newValue;
        return _lastPercents;
    }
};
