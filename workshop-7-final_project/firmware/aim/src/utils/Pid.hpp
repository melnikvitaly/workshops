#pragma once
#include <cmath>
#include "Ema.hpp"

// PID controller for a plant that *integrates* its input.
//
// The output is a RATE (deg/s), which the caller integrates into a position:
//
//     angle += pid.update(error, dt) * dt;
//
// That is the correct shape here because servo velocity -> gimbal angle -> dot
// position is an integrator (P(s) = k/s). At steady state the error is zero, so
// the output is zero and the axis holds where it is.
//
// The setpoint is implicitly ZERO: the vision system reports the error directly
// (how far the laser dot is from the target) and the whole job is to drive that
// to nothing. Because the setpoint never moves, a setpoint step can never kick
// the derivative, so differentiating the *error* is safe here. (With a moving
// setpoint you would have to differentiate the measurement instead.)
//
// The derivative is low-pass filtered. The error arrives from a camera at a
// modest frame rate and carries pixel noise; raw differentiation of that is
// mostly noise amplification. derivAlpha = 1.0 disables the filter.
class Pid
{
    float _kp, _ki, _kd;
    float _outMin, _outMax;

    float _integral  = 0.0f; // accumulated error*dt; Ki is applied at output time
    float _prevError = 0.0f;
    bool  _primed    = false;

    Ema<float> _dFilter;

public:
    Pid(float kp, float ki, float kd,
        float outMin, float outMax,
        float derivAlpha = 1.0f)
        : _kp(kp), _ki(ki), _kd(kd),
          _outMin(outMin), _outMax(outMax),
          _dFilter(derivAlpha) {}

    // Drop all accumulated state. Call whenever the loop has been open for a
    // while (signal lost, disarmed) so a stale integral cannot kick the gimbal
    // the moment tracking resumes.
    void reset()
    {
        _integral  = 0.0f;
        _prevError = 0.0f;
        _primed    = false;
        _dFilter.reset();
    }

    // Retune live without disturbing the accumulated state, so the axis does
    // not jump when a gain changes mid-flight.
    void setGains(float kp, float ki, float kd) { _kp = kp; _ki = ki; _kd = kd; }

    // On target: keep the derivative history continuous, but neither integrate
    // nor produce an output.
    //
    // This exists because feeding update() a zeroed error is NOT the same as
    // stopping. With Ki non-zero the output is still Ki*integral, so a zeroed
    // error leaves the accumulated term quietly driving the axis and the dot
    // creeps off target. Freezing is what "we have arrived" actually means.
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
    // Returns the rate command, clamped to [outMin, outMax].
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

        // Anti-windup, part 1 - conditional integration. Only accumulate when
        // doing so would not push an already-saturated output further into the
        // stop. Without this the integral keeps growing while the gimbal sits
        // against a travel limit, and the axis lags badly on the way back.
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
