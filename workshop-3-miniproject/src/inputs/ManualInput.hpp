#pragma once
#include "Input.hpp"
#include "Potentiometer.hpp"
#include "Encoder.hpp"

// Turns the physical controls into a unit (x, y) target for the gimbal.
// This is the only place that decides which control drives which axis, so the
// mapping can be changed without touching the gimbal.
//
//   x (pan)  <- potentiometer  (absolute: knob angle == pan angle)
//   y (tilt) <- encoder        (incremental: rotate to accumulate, clamped)
//
// Both controls already produce a normalised [-1, 1] reading centred at 0, so
// this source just forwards them as the unit target. The gimbal's ViewPort
// turns that unit point into servo angles. The shared control button drives the
// laser via the default Input handlers.
class ManualInput : public Input
{
    Potentiometer& _pot;
    Encoder&       _enc;

public:
    ManualInput(Potentiometer& pot, Encoder& enc)
        : _pot(pot), _enc(enc) {}

    const char *name() const override { return "manual"; }
    void init() override { _pot.init(); _enc.init(); }
    void tick() override { _enc.poll(); }  // don't miss steps

    // Latest target as unit coordinates in [-1, 1] ((0,0) at the centre).
    Command currentTarget() override
    {
        return Command::moveTo({ _pot.position(), _enc.position() });
    }
};
