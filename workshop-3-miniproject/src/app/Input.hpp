#pragma once

// Common interface for every (x, y) target source (ManualInput, AutoInput,
// JoystickInput, ...). main.cpp keeps a list of these and the on-board button
// cycles which one is active at runtime, so the loop only ever talks to an
// Input* and never cares which concrete source is driving the gimbal.
class Input
{
public:
    virtual ~Input() = default;

    // Configure any hardware this source needs. Must be safe to call once per
    // source even when several sources share a peripheral (see ADC::init).
    virtual void init() = 0;

    // Service time-critical hardware; called from the fast loop.
    virtual void poll() = 0;

    // Latest target, already mapped into the active view port.
    virtual void read(float& x, float& y) = 0;
};
