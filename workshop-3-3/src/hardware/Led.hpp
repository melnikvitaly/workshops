#pragma once
#include <hardware/PWM.hpp>

class LED
{
    PWM &_pwm;

public:
    explicit LED(PWM &pwm) : _pwm(pwm) {}

    void init()              { _pwm.init(); }
    void on()                { _pwm.max(); }
    void off()               { _pwm.off(); }
    void power(uint8_t pct)  { _pwm.power(pct); }
};
