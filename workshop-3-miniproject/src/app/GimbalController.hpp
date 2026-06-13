#pragma once
#include "Servo.hpp"

// Drives the two-axis gimbal from a normalised target position.
//
// Per the README the coordinate system has its origin (0,0) at the centre,
// with each axis spanning [-1 .. +1]:
//
//        +y (tilt up)
//         ^
//         |
//  -x <---+---> +x   (pan)
//         |
//         v
//        -y (tilt down)
//
// Each axis is mapped onto a configurable [minDeg .. maxDeg] servo range, so
// the gimbal can be limited to whatever the mechanics safely allow. -1 maps to
// minDeg, +1 to maxDeg, and 0 to their midpoint.
//
// It deliberately knows NOTHING about potentiometers or encoders - it only
// accepts an (x, y) target, so the control source can be swapped freely
// (manual input, an automated sweep, a network command, ...).
class GimbalController
{
    static constexpr float NORM_MIN        = -1.0f;   // target axis lower bound
    static constexpr float NORM_MAX        =  1.0f;   // target axis upper bound
    static constexpr float DEFAULT_MIN_DEG = 10.0f;
    static constexpr float DEFAULT_MAX_DEG = 170.0f;

    Servo& _pan;    // servo A (below) - horizontal axis
    Servo& _tilt;   // servo B (above) - vertical axis

    // Allowed servo travel (degrees) for each axis.
    float _panMin, _panMax;
    float _tiltMin, _tiltMax;

    float _x = 0.0f, _y = 0.0f;

    static float clamp(float v)
    {
        if (v < NORM_MIN) return NORM_MIN;
        if (v > NORM_MAX) return NORM_MAX;
        return v;
    }

    // Map a normalised axis value [-1, 1] onto [lo, hi] degrees.
    static float mapAxis(float n, float lo, float hi)
    {
        float t = (n - NORM_MIN) / (NORM_MAX - NORM_MIN);   // 0 .. 1
        return lo + t * (hi - lo);
    }

public:
    GimbalController(Servo& pan, Servo& tilt,
                     float panMin = DEFAULT_MIN_DEG, float panMax = DEFAULT_MAX_DEG,
                     float tiltMin = DEFAULT_MIN_DEG, float tiltMax = DEFAULT_MAX_DEG)
        : _pan(pan), _tilt(tilt),
          _panMin(panMin), _panMax(panMax),
          _tiltMin(tiltMin), _tiltMax(tiltMax) {}

    void init()
    {
        _pan.init();
        _tilt.init();
        center();
    }

    // Move to a normalised target. x, y are clamped to [-1, 1].
    void moveTo(float x, float y)
    {
        _x = clamp(x);
        _y = clamp(y);
        _pan.write(mapAxis(_x, _panMin, _panMax));
        _tilt.write(mapAxis(_y, _tiltMin, _tiltMax));
    }

    void center() { moveTo(0.0f, 0.0f); }

    float x() const { return _x; }
    float y() const { return _y; }
};
