#pragma once
#include <hardware/PWM.hpp>

class Motor
{
    PWM    &_pwm;
    uint8_t _cutOffPct;
    uint8_t _cutOnPct;
    uint8_t _minPct;
    uint8_t _maxPct;

public:
    Motor(PWM &pwm,
          uint8_t cutOffPct, uint8_t cutOnPct,
          uint8_t minPct,    uint8_t maxPct)
        : _pwm(pwm),
          _cutOffPct(cutOffPct), _cutOnPct(cutOnPct),
          _minPct(minPct), _maxPct(maxPct) {}

    void init()             { _pwm.init(); }
    void on()               { _pwm.max(); }
    void off()              { _pwm.off(); }
    void power(uint8_t pct) { _pwm.power(pct); }

    uint8_t powerMapped(uint8_t pct)
    {
        if (pct <= _cutOffPct) { _pwm.off(); return 0; }
        if (pct >= _cutOnPct)  { _pwm.max(); return 100; }

        uint8_t mapped = (uint8_t)(_minPct +
            (pct - _cutOffPct) * (_maxPct - _minPct) / (_cutOnPct - _cutOffPct));
        _pwm.power(mapped);
        return mapped;
    }
};
