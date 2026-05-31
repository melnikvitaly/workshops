#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hardware/PWM.hpp>

class LED
{
public:
    enum class State : uint8_t { ON, DIM, OFF, BLINKING };

private:
    PWM &_pwm;
    State _state = State::OFF;
    uint32_t _blinkMs = 1000;
    uint32_t _lastChangeMs = 0;
    bool _blinkPhase = false;

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

public:
    explicit LED(PWM &pwm) : _pwm(pwm) {}

    void init()  { _pwm.init(); }
    void on()    { _pwm.max(); _state = State::ON; }
    void off()   { _pwm.off(); _state = State::OFF; }

    void power(uint8_t percents)
    {
        _pwm.power(percents);
        _state = State::DIM;
    }

    void blink(uint32_t periodMs)
    {
        _state        = State::BLINKING;
        _blinkMs      = periodMs;
        _lastChangeMs = nowMs();
        _blinkPhase   = false;
    }

    State state() const { return _state; }

    const char *stateStr() const
    {
        switch (_state)
        {
            case State::ON:       return "ON";
            case State::DIM:      return "DIM";
            case State::OFF:      return "OFF";
            case State::BLINKING: return "BLINK";
        }
        return "";
    }

    void tick()
    {
        if (_state != State::BLINKING) return;
        uint32_t now = nowMs();
        if (now - _lastChangeMs > _blinkMs)
        {
            _lastChangeMs = now;
            _blinkPhase   = !_blinkPhase;
            _blinkPhase ? _pwm.on() : _pwm.off();
        }
    }
};
