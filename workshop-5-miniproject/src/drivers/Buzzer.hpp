#pragma once
#include "PWM.hpp"

// Passive buzzer driven by a PWM channel.
//
// Ported from workshop-3-4 unchanged in spirit: the mini-project's PWM is a
// superset of 3-4's (it adds setFreq/power/off), so this drops in as-is. Each
// note just retunes the PWM frequency and drives it at ~50% duty - a square
// wave the buzzer turns into a tone. Scheduling / auto-silence is the Beeper's
// job; this class only makes or stops a sound.
class Buzzer
{
    static constexpr uint8_t DUTY_PCT = 50;   // square wave for the buzzer

    PWM& _pwm;

public:
    explicit Buzzer(PWM& pwm) : _pwm(pwm) {}

    void init() { _pwm.init(); }

    void playNote(uint32_t freqHz)
    {
        _pwm.setFreq(freqHz);
        _pwm.power(DUTY_PCT);
    }

    void silence() { _pwm.off(); }
};
