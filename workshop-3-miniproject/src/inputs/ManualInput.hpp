#pragma once
#include "Input.hpp"
#include "Potentiometer.hpp"

//   x [-1,1] (pan)  <- potentiometer  (absolute: knob angle == pan angle)
//   y [-1,1] (tilt) <- encoder        (incremental: rotate to accumulate, clamped)
//
// The encoder backend is a template parameter so the same source works with
// either the software-polled Encoder or the hardware EncoderPcnt — both expose
// the same init()/poll()/position() interface, so nothing else here changes.
template <typename EncoderT>
class ManualInput : public Input
{
    Potentiometer &_pot;
    EncoderT &_enc;

public:
    ManualInput(Potentiometer &pot, EncoderT &enc)
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
