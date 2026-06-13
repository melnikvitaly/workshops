#pragma once
#include "Relay.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Laser switched through a relay.
//
// Two things can keep it lit:
//   - a persistent "constant" latch  (toggled by a long press)
//   - a transient timed "flash"      (fired by a short click)
//
// The relay is on whenever EITHER is active. A short click always ends OFF: it
// clears the latch and runs the timed flash, so flashing while constantly-on
// plays the flash and then turns the laser off.
class Laser
{
    Relay&   _relay;
    uint32_t _flashMs;
    bool     _constant     = false;   // persistent latched state
    bool     _flashing     = false;   // transient timed flash
    uint32_t _flashUntilMs = 0;

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    void apply() { (_constant || _flashing) ? _relay.on() : _relay.off(); }

public:
    Laser(Relay& relay, uint32_t flashMs) : _relay(relay), _flashMs(flashMs) {}

    void init() { _relay.init(); }

    // Fire a timed flash and end OFF: clears the constant latch, so a click
    // while constantly-on flashes and then switches the laser off.
    void flash()
    {
        _constant     = false;
        _flashing     = true;
        _flashUntilMs = nowMs() + _flashMs;
        apply();
    }

    // Latch the laser permanently on, or off if already latched on.
    void toggleConstant()
    {
        _constant = !_constant;
        apply();
    }

    // Call often; ends a flash once its duration is over.
    void update()
    {
        if (_flashing && (int32_t)(nowMs() - _flashUntilMs) >= 0)
        {
            _flashing = false;
            apply();
        }
    }

    bool isOn() const       { return _constant || _flashing; }
    bool isConstant() const { return _constant; }
};
