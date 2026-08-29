#pragma once
#include "Clock.hpp"
#include "Config.hpp"
#include "hardware/Debug.hpp"
#include "hardware/Led.hpp"

// Unchanged from workshop 2-3: a non-blocking state machine.
//
// It would be tempting to let the alert task simply vTaskDelay(5000) and then
// vTaskDelay(3000), but that costs the ability to react to anything during the
// sequence. Keeping the state machine means the alert task stays responsive and
// the RTOS only replaces *how it is scheduled*, not how it behaves.
class AlertController
{
    enum class State : uint8_t
    {
        IDLE,
        ALERT,
        BLINK
    };

    LED     &_led1;
    LED     &_led2;
    State    _state         = State::IDLE;
    uint32_t _stateStartedAt = 0;
    Debug    dbg;

public:
    AlertController(LED &led1, LED &led2, Debug dbg) : _led1(led1), _led2(led2), dbg(dbg) {}

    // Called from the button task (see README: shared-state note).
    void onButtonPress()
    {
        if (_state != State::IDLE)
            return;
        _led1.on();
        _stateStartedAt = nowMs();
        _state          = State::ALERT;
        dbg.print("alert");
    }

    void tick()
    {
        if (_state == State::ALERT && nowMs() - _stateStartedAt >= Config::ALERT_DURATION_MS)
        {
            _led1.off();
            _led2.blink(Config::BLINK_INTERVAL_MS);
            _state          = State::BLINK;
            _stateStartedAt = nowMs();
            dbg.print("blink");
        }
        else if (_state == State::BLINK && nowMs() - _stateStartedAt >= Config::BLINK_DURATION_MS)
        {
            _led2.off();
            _state = State::IDLE;
            dbg.print("idle");
        }
    }
};
