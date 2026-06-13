#pragma once
#include "Input.hpp"
#include "Potentiometer.hpp"

// Drop-in input source for a 2-axis analog joystick. A joystick is just two
// potentiometers (one per axis), each reading a normalised [-1, 1] with the
// centre/rest position at 0. This source just forwards those readings as the
// unit target; the gimbal's ViewPort turns the unit point into servo angles.
// The shared control button drives the laser via the default Input handlers.
//
// TODO: not implemented yet. Likely additions when wired up:
//   - per-axis dead-zone around centre (joysticks rarely rest at exactly 0)
//   - optional axis inversion / swap to match the physical mounting
//   - override onEvent() to map a joystick trigger to its own action
class JoystickInput : public Input
{
    Potentiometer& _xAxis;
    Potentiometer& _yAxis;

public:
    JoystickInput(Potentiometer& xAxis, Potentiometer& yAxis)
        : _xAxis(xAxis), _yAxis(yAxis) {}

    const char *name() const override { return "joystick"; }
    void init() override { _xAxis.init(); _yAxis.init(); }
    // Both axes are absolute (pure ADC reads), so nothing to poll.
    void tick() override {}

    // Latest target as unit coordinates in [-1, 1] ((0,0) at the centre).
    Command currentTarget() override
    {
        // TODO: apply dead-zone / inversion to the unit reading here.
        return Command::moveTo({ _xAxis.position(), _yAxis.position() });
    }
};
