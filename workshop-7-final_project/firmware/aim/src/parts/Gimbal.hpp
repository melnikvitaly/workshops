#pragma once
#include <cmath>
#include "Servo.hpp"
#include "Point.hpp"
#include "ViewPort.hpp"

// Drives the two-axis gimbal.
//
//   _pan  - servo A (below) - horizontal axis
//   _tilt - servo B (above) - vertical axis
//
// This gimbal is commanded by VELOCITY, not by position: setVelocity() states
// how fast each axis should be turning and update() integrates that into an
// angle each control step. That makes the
// gimbal the integrator in the control loop - the PID upstream outputs a rate,
// and the angle is its integral - and it means motion is inherently rate
// limited, so the servos ramp instead of slamming between positions.
//
// Travel is clamped to the intersection of the hard mechanical limits and the
// ViewPort working window, so a runaway loop or a sign error cannot drive the
// arm into its stop.
class Gimbal
{
    static constexpr float DEFAULT_MIN_DEG = 10.0f;
    static constexpr float DEFAULT_MAX_DEG = 170.0f;
    static constexpr float DEFAULT_MAX_RATE = 120.0f; // deg/s

    Servo &_pan;  // servo A (below) - horizontal axis
    Servo &_tilt; // servo B (above) - vertical axis

    ViewPort _viewPortAngles; // working window (degrees); its centre is the park pose

    // Effective travel: mechanical limits narrowed to the working window.
    float _panLo, _panHi;
    float _tiltLo, _tiltHi;

    // Hard ceiling on rotation speed (deg/s), applied to every commanded
    // velocity. Protection, not tuning - see Config.hpp.
    float _panMaxRate, _tiltMaxRate;

    Point _velocity{0.0f, 0.0f}; // deg/s, already rate-limited

    static float clampAngle(float deg, float lo, float hi)
    {
        if (deg < lo)
            return lo;
        if (deg > hi)
            return hi;
        return deg;
    }

    // Symmetric clamp with a NaN guard: a non-finite rate would poison the
    // integrated angle permanently, and the servo would never recover.
    static float clampRate(float degPerSec, float maxRate)
    {
        if (!std::isfinite(degPerSec))
            return 0.0f;
        if (degPerSec > maxRate)
            return maxRate;
        if (degPerSec < -maxRate)
            return -maxRate;
        return degPerSec;
    }

    static float maxf(float a, float b) { return a > b ? a : b; }
    static float minf(float a, float b) { return a < b ? a : b; }

public:
    Gimbal(Servo &pan, Servo &tilt,
           ViewPort viewPort,
           float panMin = DEFAULT_MIN_DEG, float panMax = DEFAULT_MAX_DEG,
           float tiltMin = DEFAULT_MIN_DEG, float tiltMax = DEFAULT_MAX_DEG,
           float panMaxRate = DEFAULT_MAX_RATE, float tiltMaxRate = DEFAULT_MAX_RATE)
        : _pan(pan), _tilt(tilt),
          _viewPortAngles(viewPort),
          _panLo(maxf(panMin, viewPort.center.x - viewPort.halfWidth())),
          _panHi(minf(panMax, viewPort.center.x + viewPort.halfWidth())),
          _tiltLo(maxf(tiltMin, viewPort.center.y - viewPort.halfHeight())),
          _tiltHi(minf(tiltMax, viewPort.center.y + viewPort.halfHeight())),
          _panMaxRate(panMaxRate), _tiltMaxRate(tiltMaxRate)
    {
    }

    void init()
    {
        _pan.init();
        _tilt.init();
        park();
    }

    ViewPort viewPort() const { return _viewPortAngles; }

    // Command the axis rates (deg/s), clamped to the hard rate ceiling. Held
    // until the next call, so a dropped frame does not stall the motion
    // mid-step - and so the failsafe only has to write zeroes once.
    //
    // The clamp lives here rather than in the controller so it holds for every
    // caller: a mistuned PID, a future input source, or a sign error cannot get
    // around it.
    void setVelocity(Point degPerSec)
    {
        _velocity = {clampRate(degPerSec.x, _panMaxRate),
                     clampRate(degPerSec.y, _tiltMaxRate)};
    }

    float panMaxRate() const  { return _panMaxRate; }
    float tiltMaxRate() const { return _tiltMaxRate; }

    void stop() { _velocity = {0.0f, 0.0f}; }

    Point velocity() const { return _velocity; }

    // Integrate the commanded rate into an angle and drive the servos. Call
    // once per control step with the elapsed time in seconds.
    void update(float dtSec)
    {
        moveTo(_pan.angle() + _velocity.x * dtSec,
               _tilt.angle() + _velocity.y * dtSec);
    }

    // Drive both servos to the given angles (degrees), clamped to the
    // effective travel.
    void moveTo(float panDeg, float tiltDeg)
    {
        _pan.write(clampAngle(panDeg, _panLo, _panHi));
        _tilt.write(clampAngle(tiltDeg, _tiltLo, _tiltHi));
    }

    // Displace both axes by a relative amount, ignoring the rate limit.
    //
    // For step-response experiments: knock the gimbal off target by a known
    // number of degrees and watch the loop pull it back. Because the
    // displacement is identical every time, two gain sets can actually be
    // compared - which a hand-moved target cannot give you.
    void nudge(Point deg)
    {
        moveTo(_pan.angle() + deg.x, _tilt.angle() + deg.y);
    }

    // Park at the centre of the working window and stand still.
    void park()
    {
        stop();
        moveTo(_viewPortAngles.center.x, _viewPortAngles.center.y);
    }

    // True while an axis is pressed against its travel limit - the loop cannot
    // reduce the error any further in that direction.
    bool atLimit() const
    {
        return _pan.angle() <= _panLo || _pan.angle() >= _panHi ||
               _tilt.angle() <= _tiltLo || _tilt.angle() >= _tiltHi;
    }

    float panAngle() const { return _pan.angle(); }
    float tiltAngle() const { return _tilt.angle(); }
};
