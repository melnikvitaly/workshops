#pragma once
#include "Relay.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Laser switched through a relay.
//
// Unlike the workshop-3 version, the laser here is normally held CONSTANT ON -
// the camera can only measure the tracking error while the dot is visible. That
// inverts what "fire" has to mean: a pulse that switches the laser *on* would be
// invisible against an already-lit beam, so firing instead blanks the beam for a
// short moment and then restores it. The visible event is the gap.
//
// Two pieces of state:
//   - _constant : the persistent latch (what the beam does when left alone)
//   - _blanking : a transient override that forces the beam off while firing
//
// Blanking wins over the latch, and expires on its own in tick().
class Laser
{
    Relay   &_relay;
    uint32_t _blankMs;
    bool     _constant     = false;
    bool     _blanking     = false;
    uint32_t _blankUntilMs = 0;

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    void apply() { (_constant && !_blanking) ? _relay.on() : _relay.off(); }

public:
    Laser(Relay &relay, uint32_t blankMs) : _relay(relay), _blankMs(blankMs) {}

    void init() { _relay.init(); }

    void on()
    {
        _constant = true;
        apply();
    }

    void off()
    {
        _constant = false;
        apply();
    }

    void toggleConstant()
    {
        _constant = !_constant;
        apply();
    }

    // Fire: blank the beam for _blankMs, then restore whatever the latch says.
    //
    // Note this is a no-op to the eye when the laser is already off - there is
    // no beam to interrupt. Firing deliberately does NOT switch the laser on:
    // a 5 mW emitter should only ever light because someone asked it to, not as
    // a side effect of a command that means "blink".
    void fire()
    {
        _blanking     = true;
        _blankUntilMs = nowMs() + _blankMs;
        apply();
    }

    // Call often; ends the blank once its duration is over.
    void tick()
    {
        if (_blanking && (int32_t)(nowMs() - _blankUntilMs) >= 0)
        {
            _blanking = false;
            apply();
        }
    }

    bool isOn() const       { return _constant && !_blanking; }
    bool isConstant() const { return _constant; }
    bool isFiring() const   { return _blanking; }
};
