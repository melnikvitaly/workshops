#pragma once
#include <cmath>
#include "Ema.hpp"

// PID controller for a plant with its own near-unity position loop (a hobby
// servo obeying a position command directly) - the direct-position form from
// docs/servo-control-strategies.md SS1, as opposed to Pid.hpp's velocity form.
//
// The output IS the commanded position (here, a unit offset the caller scales
// onto the working zone - see AutoPositionChannel):
//
//     unit = pid.update(error, dt);
//
// Unlike Pid.hpp, the plant here does not integrate the output, so there is
// no creep to guard against: a non-zero Ki*integral at zero error is exactly
// the steady holding offset the loop needs (e.g. countering gravity droop on
// tilt), not a runaway command. hold() below exists only to keep the
// derivative history continuous while frozen, matching Pid::hold()'s shape.
//
// The setpoint is implicitly ZERO, same as Pid.hpp: the vision system reports
// the error directly, and the job is to drive it to nothing.
class PidPosition
{
    float _kp, _ki, _kd;
    float _outMin, _outMax;

    float _integral  = 0.0f;
    float _prevError = 0.0f;
    bool  _primed    = false;

    Ema<float> _dFilter;

public:
    PidPosition(float kp, float ki, float kd,
                float outMin, float outMax,
                float derivAlpha = 1.0f)
        : _kp(kp), _ki(ki), _kd(kd),
          _outMin(outMin), _outMax(outMax),
          _dFilter(derivAlpha) {}

    // Drop all accumulated state - same call sites as Pid::reset() (channel
    // switch, link recovered, arm, e-stop, ...).
    void reset()
    {
        _integral  = 0.0f;
        _prevError = 0.0f;
        _primed    = false;
        _dFilter.reset();
    }

    // Retune live without disturbing the accumulated state.
    void setGains(float kp, float ki, float kd) { _kp = kp; _ki = ki; _kd = kd; }

    // Keep the derivative history continuous for a tick where the caller
    // chooses not to command a new position (e.g. inside the deadzone).
    void hold(float error)
    {
        _prevError = error;
        _primed    = true;
    }

    float kp() const       { return _kp; }
    float ki() const       { return _ki; }
    float kd() const       { return _kd; }
    float integral() const { return _integral; }

    // error - measured error (target minus laser dot), normalised to [-1, 1].
    // dt    - seconds since the previous call.
    // Returns the position command, clamped to [outMin, outMax].
    float update(float error, float dt)
    {
        if (dt <= 0.0f || !std::isfinite(error))
            return 0.0f;

        float derivative = 0.0f;
        if (_primed)
            derivative = _dFilter.update((error - _prevError) / dt);
        else
            _primed = true; // first sample has no meaningful slope
        _prevError = error;

        const float p = _kp * error;
        const float d = _kd * derivative;

        // Anti-windup, part 1 - conditional integration, same shape as
        // Pid.hpp: don't accumulate while doing so would only push an
        // already-saturated output further into the clamp.
        float out = p + _ki * _integral + d;
        const bool pushingIntoStop = (out >= _outMax && error > 0.0f) ||
                                     (out <= _outMin && error < 0.0f);
        if (!pushingIntoStop)
        {
            _integral += error * dt;

            // Anti-windup, part 2 - cap the integral at the value that alone
            // saturates the output, so it can never dominate P and D.
            if (_ki > 0.0f)
            {
                const float limit = _outMax / _ki;
                if (_integral > limit)
                    _integral = limit;
                if (_integral < -limit)
                    _integral = -limit;
            }
            out = p + _ki * _integral + d;
        }

        if (out > _outMax)
            out = _outMax;
        if (out < _outMin)
            out = _outMin;
        return out;
    }
};
