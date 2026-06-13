#pragma once
#include "Input.hpp"
#include "Potentiometer.hpp"
#include "ViewPort.hpp"

// Drop-in input source for a 2-axis analog joystick. A joystick is just two
// potentiometers (one per axis), each reading a normalised [-1, 1] with the
// centre/rest position at 0. Both axes are mapped into the ViewPort.
//
// Same interface (init / poll / read) as ManualInput / AutoInput, so selecting
// it only changes which type main.cpp constructs.
//
// TODO: not implemented yet. Likely additions when wired up:
//   - per-axis dead-zone around centre (joysticks rarely rest at exactly 0)
//   - optional axis inversion / swap to match the physical mounting
class JoystickInput : public Input
{
    Potentiometer& _xAxis;
    Potentiometer& _yAxis;
    ViewPort       _vp;

    // Map a normalised [-1, 1] reading to the [0, 1] fraction lerp() expects.
    static float toFraction(float n) { return 0.5f * (n + 1.0f); }

public:
    JoystickInput(Potentiometer& xAxis, Potentiometer& yAxis, const ViewPort& vp)
        : _xAxis(xAxis), _yAxis(yAxis), _vp(vp) {}

    void init() override
    {
        _xAxis.init();
        _yAxis.init();
    }

    // Both axes are absolute (pure ADC reads), so there is nothing to service
    // in the fast loop. Kept for interface parity with ManualInput.
    void poll() override {}

    // Latest target, mapped into the view port.
    void read(float& x, float& y) override
    {
        // TODO: implement -- apply dead-zone / inversion before mapping.
        x = _vp.lerpX(toFraction(_xAxis.position()));
        y = _vp.lerpY(toFraction(_yAxis.position()));
    }
};
