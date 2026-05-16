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
    Debug &dbg;
    ADC(uint8_t pin, u_int16_t resolution, Debug &dbg) : _pin(pin), _resolution(resolution), dbg(dbg)
    {
    }

    void init()
    {
        pinMode(_pin, INPUT);
    }

    uint8_t percent()
    {
        auto value = analogRead(_pin);

        if (abs(_lastValue - value) < HYSTERESIS)
        {
            return _lastPercents;
        }
        _lastValue = value;
        auto newValue = (uint8_t)((float)value / _resolution * 100);

        if (abs(_lastPercents - newValue) >= HYSTERESIS_PERCENTS)
        {
            dbg.print(String("ADC:") + String(_lastPercents) + "=>" + newValue);
        }
        _lastPercents = newValue;
        return _lastPercents;
    }
};