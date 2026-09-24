#pragma once
#include <cmath>
#include <cstdint>
#include "IInputChannel.hpp"
#include "Gimbal.hpp"
#include "Point.hpp"
#include "Zone.hpp"
#include "Gains.hpp"
#include "VelocityEquationPid.hpp"
#include "Config.hpp"

// Camera-tracking input channel, velocity-equation form: a pan/tilt
// VelocityEquationPid pair that commands a servo-angle DELTA on every fresh
// frame, instead of AutoPositionalChannel's rate + integration. See
// docs/servo-control-strategies.md for why this form is an alternative, not
// the default, for this plant.
//
// Structurally a twin of AutoPositionalChannel (same frame-driven dt, same
// per-axis deadzone handling) - only the last step differs: each PID's delta
// is added straight onto the current servo angle and written with moveTo(),
// same pattern as Gimbal::nudge(), instead of integrated with
// setVelocity()+Gimbal::update().
class AutoVelocityEquationChannel : public IInputChannel
{
public:
    AutoVelocityEquationChannel(Gimbal &gimbal,
                                 float panKp, float panKi, float panKd,
                                 float tiltKp, float tiltKi, float tiltKd,
                                 float derivAlpha)
        : _gimbal(gimbal),
          _panPid(panKp, panKi, panKd, 0.0f, 0.0f, derivAlpha),
          _tiltPid(tiltKp, tiltKi, tiltKd, 0.0f, 0.0f, derivAlpha)
    {
    }

    // A camera ErrorSample arrived off cmd_q - identical contract to
    // AutoPositionalChannel::onErrorSample().
    void onErrorSample(bool visible, Point error, uint32_t frameMs)
    {
        _targetVisible = visible;
        if (_targetVisible)
            _error = error;
        _frameMs    = frameMs;
        _frameReady = true;
    }

    // Live retune without disturbing the accumulated state.
    void setPanGains(float kp, float ki, float kd)  { _panPid.setGains(kp, ki, kd); }
    void setTiltGains(float kp, float ki, float kd) { _tiltPid.setGains(kp, ki, kd); }

    // The gains actually running, straight from each VelocityEquationPid -
    // not a separate copy that could drift from what update() uses.
    Gains panGains() const  { return {_panPid.kp(),  _panPid.ki(),  _panPid.kd()};  }
    Gains tiltGains() const { return {_tiltPid.kp(), _tiltPid.ki(), _tiltPid.kd()}; }

    // Full reset: channel switch, tour-done, arm, link-recovered, fault-ack,
    // e-stop, link-stale. Re-seeds each PID's clamp reference at the gimbal's
    // actual current angle (not zero) and rebinds the clamp to the current
    // working-zone bounds, so the anti-windup limit reflects real, reachable
    // positions - see VelocityEquationPid::reset()/setOutputLimits().
    void reset(uint32_t now) override
    {
        syncOutputLimits();
        _panPid.reset(_gimbal.panAngle());
        _tiltPid.reset(_gimbal.tiltAngle());
        _frameReady = false;
        _onTarget   = false;
        _lastPidMs  = now;
    }

    Point    error() const    { return _error; }
    bool     onTarget() const { return _onTarget; }
    // PID evaluations since boot (one per fresh frame) - the caller derives Hz.
    uint32_t pidRuns() const  { return _pidRuns; }
    // True if either axis' PID had to trim its running output this tick -
    // the loop is saturated, distinct from Gimbal::wasClamped() (the
    // travel-zone clamp moveTo() itself applies downstream).
    bool     pidClamped() const { return _panPid.wasClamped() || _tiltPid.wasClamped(); }

    // Drive the gimbal for this tick. `fresh` is the caller's link/frame
    // freshness test - false forces a reset, same as a lost link. Unlike
    // AutoPositionalChannel there is nothing to command explicitly to "hold":
    // moveTo() simply isn't called again for an axis, and the servo stays
    // exactly where it was last written.
    void update(uint32_t now, bool fresh) override
    {
        if (!fresh)
        {
            reset(now);
            return;
        }
        if (!_targetVisible)
        {
            _lastPidMs = now;
            return;
        }
        if (!_frameReady)
            return; // nothing new - the gimbal holds the last commanded position

        // The working zone can change live (Gimbal::setWorkingZone(), e.g. a
        // cfg.set from the UI) without a channel reset - rebind the clamp
        // every tick so it never runs on a stale zone.
        syncOutputLimits();

        float dt = (float)(_frameMs - _lastPidMs) / 1000.0f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.5f)   dt = 0.5f;
        _lastPidMs  = _frameMs;
        _frameReady = false;
        ++_pidRuns;

        const float panDeg  = axisDelta(_panPid, _error.x, dt) + _gimbal.panAngle();
        const float tiltDeg = axisDelta(_tiltPid, _error.y, dt) + _gimbal.tiltAngle();
        _gimbal.moveTo(panDeg, tiltDeg); // clamped to the effective travel zone
        updateArrival();
    }

private:
    // Below the deadzone, hold this axis exactly where it is instead of
    // committing to an arbitrary delta inside the servo's own deadband - same
    // reasoning as AutoPositionalChannel::axisRate(), just returning a delta
    // instead of a rate.
    static float axisDelta(VelocityEquationPid &pid, float error, float dt)
    {
        const float mag = error < 0.0f ? -error : error;
        if (mag < config::TRACK_DEADZONE)
        {
            pid.hold(error);
            return 0.0f;
        }
        return pid.update(error, dt);
    }

    // Absolute angle bounds, straight from the gimbal's live working zone -
    // same rectangle Gimbal::moveTo() itself clamps into, so this PID-side
    // clamp cannot fall out of sync with what's actually reachable.
    void syncOutputLimits()
    {
        const Zone z = _gimbal.viewPort().toZone();
        _panPid.setOutputLimits(z.panMin, z.panMax);
        _tiltPid.setOutputLimits(z.tiltMin, z.tiltMax);
    }

    void updateArrival()
    {
        const float ax = _error.x < 0.0f ? -_error.x : _error.x;
        const float ay = _error.y < 0.0f ? -_error.y : _error.y;
        _onTarget = ax < config::TRACK_DEADZONE && ay < config::TRACK_DEADZONE;
    }

    Gimbal &_gimbal;
    VelocityEquationPid _panPid;
    VelocityEquationPid _tiltPid;

    Point    _error{0.0f, 0.0f};
    bool     _targetVisible = false;
    bool     _frameReady    = false;
    bool     _onTarget      = false;
    uint32_t _frameMs       = 0;
    uint32_t _lastPidMs     = 0;
    uint32_t _pidRuns       = 0;
};
