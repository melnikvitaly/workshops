#pragma once
#include <cstdint>
#include "Point.hpp"

enum class CommandType
{
    SetVelocity, // pan/tilt rate in deg/s - the controller's motion output
    LaserFire,   // blank the beam briefly, then restore it (see Laser.hpp)
    LaserToggleConstant,
    Nudge,        // open-loop displacement in degrees, for step-response tests
    SetTelemetry, // value != 0 -> stream the per-frame log line
};

struct Command
{
    CommandType type;
    union
    {
        int32_t value;
        Point   velocity; // SetVelocity: deg/s, x = pan, y = tilt
        Point   degrees;  // Nudge: relative degrees
    };

    static Command action(CommandType type, int32_t value = 0)
    {
        Command c;
        c.type  = type;
        c.value = value;
        return c;
    }

    // The input source no longer names a place to go - it commands how fast
    // each axis should be turning. The Gimbal integrates that into an angle.
    static Command setVelocity(Point degPerSec)
    {
        Command c;
        c.type     = CommandType::SetVelocity;
        c.velocity = degPerSec;
        return c;
    }

    static Command nudge(Point deg)
    {
        Command c;
        c.type    = CommandType::Nudge;
        c.degrees = deg;
        return c;
    }
};
