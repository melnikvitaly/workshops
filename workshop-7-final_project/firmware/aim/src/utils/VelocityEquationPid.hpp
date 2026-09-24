#pragma once
#include <cmath>
#include <cstdint>
#include "Ema.hpp"

// Incremental / velocity-equation PID controller - the algorithm from
// docs/pid_controller_equations_positional_vs_velocity.md SS2, as opposed to
// PositionalPid.hpp's positional-equation form.
//
// Instead of computing an absolute control output, each sample computes the
// CHANGE in output since the previous sample:
//
//     du_k = Kp*(e_k - e_{k-1}) + Ki*e_k*dt + Kd*(e_k - 2*e_{k-1} + e_{k-2})/dt
//
// update() returns du_k directly - a delta the caller adds straight onto the
// current servo angle, with no separate rate -> position integration step:
//
//     angle += pid.update(error, dt);
//
// The setpoint is implicitly ZERO, same as PositionalPid.hpp: the vision
// system reports the error directly, and the job is to drive it to nothing.
//
// There is no explicit integral accumulator - per the reference doc, the
// running control output plays that role. This class tracks that running
// output (the sum of every delta issued since the last reset()) purely to
// clamp it: du_k is trimmed so the running output never leaves
// [outMin, outMax]. That IS the anti-windup - once the running output sits
// at a limit, a delta that would push further into it collapses to zero
// instead of accumulating, which is the "Advantages of the Velocity Form"
// windup behaviour the reference doc describes.
class VelocityEquationPid
{
    float _kp, _ki, _kd;
    float _outMin, _outMax;

    // Tracks the actual commanded angle (degrees), not a delta-from-reset
    // offset - see reset(angle) below. The clamp is anti-windup ONLY when it
    // reflects real, reachable positions; anchored to an arbitrary zero it
    // just freezes the axis once enough deltas have been issued, regardless
    // of where the target actually is.
    float _output     = 0.0f;
    float _prevError  = 0.0f; // e[k-1]
    float _prevError2 = 0.0f; // e[k-2]
    uint8_t _primed   = 0;    // 0 = no history, 1 = one sample, 2+ = full history
    bool    _clamped  = false; // true if the last update() delta was trimmed by the running-output clamp

    Ema<float> _dFilter;

public:
    VelocityEquationPid(float kp, float ki, float kd,
                         float outMin, float outMax,
                         float derivAlpha = 1.0f)
        : _kp(kp), _ki(ki), _kd(kd),
          _outMin(outMin), _outMax(outMax),
          _dFilter(derivAlpha) {}

    // Drop all accumulated state - same call sites as PositionalPid::reset()
    // (channel switch, link recovered, arm, e-stop, ...). `angle` is the
    // gimbal's actual current angle for this axis: seeding _output with it
    // (instead of 0) makes the clamp bound the real, absolute angle rather
    // than a cumulative offset from wherever the axis happened to be at
    // reset - see setOutputLimits().
    void reset(float angle = 0.0f)
    {
        _output     = angle;
        _prevError  = 0.0f;
        _prevError2 = 0.0f;
        _primed     = 0;
        _clamped    = false;
        _dFilter.reset();
    }

    // Retune live without disturbing the accumulated state.
    void setGains(float kp, float ki, float kd) { _kp = kp; _ki = ki; _kd = kd; }

    // Rebind the clamp to the current working-zone bounds (absolute degrees,
    // not a symmetric +/- window) - called every tick so a runtime zone
    // change (Gimbal::setWorkingZone()) takes effect immediately instead of
    // being frozen at construction time.
    void setOutputLimits(float outMin, float outMax) { _outMin = outMin; _outMax = outMax; }

    // Keep the error history continuous for a tick where the caller chooses
    // not to command a delta (e.g. inside the deadzone) - same call sites as
    // PositionalPid::hold().
    void hold(float error)
    {
        _prevError2 = _prevError;
        _prevError  = error;
        if (_primed < 2)
            ++_primed;
        _clamped = false; // not producing a delta this tick - nothing to clamp
    }

    float kp() const { return _kp; }
    float ki() const { return _ki; }
    float kd() const { return _kd; }
    // True if the most recent update() call had to trim the running output
    // to fit [outMin, outMax] - the loop is saturated on this axis.
    bool  wasClamped() const { return _clamped; }

    // error - measured error (target minus laser dot), normalised to [-1, 1].
    // dt    - seconds since the previous call.
    // Returns the angle delta for this tick (degrees), already trimmed so
    // the running output stays inside [outMin, outMax].
    float update(float error, float dt)
    {
        if (dt <= 0.0f || !std::isfinite(error))
            return 0.0f;

        if (_primed == 0)
        {
            // No history yet: bootstrap e[k-1] = e[k-2] = e[k] so the P and D
            // difference terms start at zero instead of an undefined jump.
            _prevError  = error;
            _prevError2 = error;
            _primed     = 1;
        }

        const float dP   = _kp * (error - _prevError);
        const float dI   = _ki * error * dt;
        const float rawD = (error - 2.0f * _prevError + _prevError2) / dt;
        const float dD   = _kd * _dFilter.update(rawD);

        _prevError2 = _prevError;
        _prevError  = error;
        if (_primed < 2)
            ++_primed;

        const float rawDelta = dP + dI + dD;

        // Anti-windup: clamp the running output, then derive the delta from
        // the clamp (see class comment).
        float candidate = _output + rawDelta;
        float clamped = candidate;
        if (clamped > _outMax) clamped = _outMax;
        if (clamped < _outMin) clamped = _outMin;
        _clamped = (clamped != candidate);
        const float delta = clamped - _output;
        _output = clamped;
        return delta;
    }
};
