#pragma once
#include "Clock.hpp"
#include "Debug.hpp"
#include "PWM.hpp"

class LED
{
public:
    enum class LEDState : uint8_t
    {
        ON,
        OFF,
        BLINKING
    };

private:
    PWM     &_pwm;
    LEDState _state                 = LEDState::OFF;
    uint32_t _blinkingTimeInState   = 1000;
    uint32_t _blinkingLastChangeTime = 0;
    bool     _blinkingLastState     = false;
    Debug    dbg;

public:
    explicit LED(PWM &pwm, Debug dbg) : _pwm(pwm), dbg(dbg) {}

    void init() { _pwm.init(); }

    void on()
    {
        _pwm.max();
        _state = LEDState::ON;
    }

    void power(uint8_t percents)
    {
        _pwm.power(percents);
        _state = percents > 0 ? LEDState::ON : LEDState::OFF;
    }

    void blink(uint32_t timeInState)
    {
        _state                  = LEDState::BLINKING;
        _blinkingTimeInState    = timeInState;
        _blinkingLastChangeTime = nowMs();
        _blinkingLastState      = false;
    }

    void off()
    {
        _pwm.off();
        _state = LEDState::OFF;
    }

    // Still a tick(): the blink toggle is driven by whichever task owns the LED.
    void tick()
    {
        if (_state != LEDState::BLINKING)
            return;

        const uint32_t now = nowMs();
        if (now - _blinkingLastChangeTime > _blinkingTimeInState)
        {
            _blinkingLastChangeTime = now;
            _blinkingLastState      = !_blinkingLastState;
            _blinkingLastState ? _pwm.on() : _pwm.off();
        }
    }
};
