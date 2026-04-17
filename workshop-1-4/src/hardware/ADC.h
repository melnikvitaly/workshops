#pragma once
#include <Arduino.h>
#include <hardware/DebugOut.h>

class ADC
{
private:
    uint8_t _pin;

public:
    uint8_t _lastPercents;
    u_int16_t _resolution;
    DebugOut &dbg;
    ADC(uint8_t pin, u_int16_t resolution, DebugOut &dbg) : _pin(pin), _resolution(resolution), dbg(dbg)
    {
    }

    void init()
    {
        pinMode(_pin, INPUT);
    }

    uint8_t percent()
    {
        auto value = analogRead(_pin);
        _lastPercents = (float)value / _resolution * 100;
        return _lastPercents;
    }
};