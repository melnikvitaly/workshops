#pragma once
#include "Input.hpp"
#include "Potentiometer.hpp"
#include "Encoder.hpp"

//   x [-1,1] (pan)  <- potentiometer  (absolute: knob angle == pan angle)
//   y [-1,1] (tilt) <- encoder        (incremental: rotate to accumulate, clamped)
class ManualInput : public Input
{
    Potentiometer &_pot;
    Encoder &_enc;

public:
    ManualInput(Potentiometer &pot, Encoder &enc)
        : _pot(pot), _enc(enc) {}

    const char *name() const override { return "manual"; }
    void init() override
    {
        _pot.init();
        _enc.init();
    }
    void tick() override { _enc.poll(); }

    // Enqueue the latest target as unit coordinates in [-1, 1] ((0,0) at the centre).
    void update() override
    {
        enqueue(Command::moveTo({_pot.position(), _enc.position()}));
    }
};
