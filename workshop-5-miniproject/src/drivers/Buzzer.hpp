#pragma once
#include "PWM.hpp"

// Passive buzzer driven by a PWM channel.
//
// Each note just retunes the PWM frequency and drives it at ~50% duty - a
// square wave the buzzer turns into a tone. Scheduling / auto-silence is the
// Beeper's job; this class only makes or stops a sound.
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
