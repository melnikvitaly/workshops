#pragma once
#include "PWM.hpp"

class Buzzer
{
    PWM& _pwm;

public:
    explicit Buzzer(PWM& pwm) : _pwm(pwm) {}

    void init() { _pwm.init(); }

    void playNote(uint32_t freqHz)
    {
        _pwm.setFreq(freqHz);
        _pwm.power(50);
    }

    void silence() { _pwm.off(); }
};
