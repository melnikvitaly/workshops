#pragma once
#include "Servo.hpp"
#include "Point.hpp"

// Drives the two-axis gimbal. The working area is supplied as a ViewPort in
// servo-angle space, so the gimbal owns the unit -> angle mapping:
//
//   _pan  - servo A (below) - horizontal axis
//   _tilt - servo B (above) - vertical axis

class Gimbal
{
    static constexpr float DEFAULT_MIN_DEG = 10.0f;
    static constexpr float DEFAULT_MAX_DEG = 170.0f;

    // The gimbal's working area (a sub-window of the full travel), in degrees.
    ViewPort _viewPortAngles;

    Servo &_pan;  // servo A (below) - horizontal axis
    Servo &_tilt; // servo B (above) - vertical axis

    // Allowed servo travel (degrees) for each axis.
    float _panMin, _panMax;
    float _tiltMin, _tiltMax;

    // Clamp an absolute angle (degrees) to the axis travel limits [lo, hi].
    static float clampAngle(float deg, float lo, float hi)
    {
        if (deg < lo)
            return lo;
        if (deg > hi)
            return hi;
        return deg;
    }

public:
    Gimbal(Servo &pan, Servo &tilt,
           ViewPort viewPort,
           float panMin = DEFAULT_MIN_DEG, float panMax = DEFAULT_MAX_DEG,
           float tiltMin = DEFAULT_MIN_DEG, float tiltMax = DEFAULT_MAX_DEG)
        : _viewPortAngles(viewPort),
          _pan(pan), _tilt(tilt),
          _panMin(panMin), _panMax(panMax),
          _tiltMin(tiltMin), _tiltMax(tiltMax)
    {
    }

    void init()
    {
        _pan.init(); // each servo parks at its own centre
        _tilt.init();
    }

    ViewPort viewPort() { return _viewPortAngles; }

    // Aim at a unit target (each axis in [-1, 1], x = pan, y = tilt): translate
    // into absolute servo angles via the ViewPort, clamp to the travel limits
    // and drive the two servos.
    void aim(Point target)
    {
        auto angles = _viewPortAngles.translate(target);
        moveTo(angles.x, angles.y);
    }

    // Drive the two servos to the given angles (degrees).
    void moveTo(float panDeg, float tiltDeg)
    {
        _pan.write(clampAngle(panDeg, _panMin, _panMax));
        _tilt.write(clampAngle(tiltDeg, _tiltMin, _tiltMax));
    }

    float panAngle() const { return _pan.angle(); }
    float tiltAngle() const { return _tilt.angle(); }
};
