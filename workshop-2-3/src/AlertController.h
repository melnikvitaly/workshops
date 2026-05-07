#pragma once
#include <Arduino.h>
#include <hardware/Led.h>
#include <hardware/Debug.h>
#include <Config.h>

class AlertController
{
    enum class State : uint8_t { IDLE, ALERT, BLINK };

    LED &_led1;
    LED &_led2;
    State _state = State::IDLE;
    unsigned long _stateStartedAt = 0;
    Debug &dbg;

public:
    AlertController(LED &led1, LED &led2, Debug &dbg) : _led1(led1), _led2(led2), dbg(dbg) {}

    void onButtonPress()
    {
        if (_state != State::IDLE)
            return;
        _led1.on();
        _state = State::ALERT;
        _stateStartedAt = millis();
        dbg.print("alert");
    }

    void tick()
    {
        if (_state == State::ALERT && millis() - _stateStartedAt >= Config::ALERT_DURATION_MS)
        {
            _led1.off();
            _led2.blink(Config::BLINK_INTERVAL_MS);
            _state = State::BLINK;
            _stateStartedAt = millis();
            dbg.print("blink");
        }
        else if (_state == State::BLINK && millis() - _stateStartedAt >= Config::BLINK_DURATION_MS)
        {
            _led2.off();
            _state = State::IDLE;
            dbg.print("idle");
        }
    }
};
