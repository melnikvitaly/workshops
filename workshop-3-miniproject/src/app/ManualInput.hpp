#pragma once
#include "Input.hpp"
#include "Potentiometer.hpp"
#include "Encoder.hpp"
#include "ViewPort.hpp"

// Turns the physical controls into an (x, y) target for the GimbalController.
// This is the only place that decides which control drives which axis, so the
// mapping can be changed without touching the gimbal.
//
//   x (pan)  <- potentiometer  (absolute: knob angle == pan angle)
//   y (tilt) <- encoder        (incremental: rotate to accumulate, clamped)
//
// Both controls produce a normalised [-1, 1] reading which is then mapped into
// the ViewPort, so the working area can be restricted in one place.
class ManualInput : public Input
{
    static constexpr float NORM_MIN         = -1.0f;
    static constexpr float NORM_MAX         =  1.0f;
    static constexpr float DEFAULT_ENC_SPAN = 20.0f;  // detents -> full deflection

    Potentiometer& _pot;
    Encoder&       _enc;
    ViewPort       _vp;

    // How many encoder detents from centre to a full +/-1 deflection.
    float _encSpan;

    // Map a normalised [-1, 1] reading to the [0, 1] fraction lerp() expects.
    static float toFraction(float n) { return 0.5f * (n + 1.0f); }

public:
    ManualInput(Potentiometer& pot, Encoder& enc, const ViewPort& vp,
                float encSpan = DEFAULT_ENC_SPAN)
        : _pot(pot), _enc(enc), _vp(vp), _encSpan(encSpan) {}

    void init() override
    {
        _pot.init();
        _enc.init();
    }

    // Service the encoder; call this from the fast loop so no steps are missed.
    void poll() override { _enc.poll(); }

    // Latest target, mapped into the view port.
    void read(float& x, float& y) override
    {
        x = _vp.lerpX(toFraction(_pot.position()));

        float e = (float)_enc.count() / _encSpan;
        if (e < NORM_MIN) e = NORM_MIN;
        if (e > NORM_MAX) e = NORM_MAX;
        y = _vp.lerpY(toFraction(e));
    }
};
