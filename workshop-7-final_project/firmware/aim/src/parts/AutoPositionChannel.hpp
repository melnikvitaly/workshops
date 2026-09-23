#pragma once
#include <cmath>
#include <cstdint>
#include "IInputChannel.hpp"
#include "Gimbal.hpp"
#include "Point.hpp"
#include "ViewPort.hpp"
#include "Gains.hpp"
#include "PidPosition.hpp"
#include "Config.hpp"

// Camera-tracking input channel, direct-position form: a pan/tilt
// PidPosition pair that commands the gimbal's position directly on every
// fresh frame, instead of AutoChannel's velocity + integration. See
// docs/servo-control-strategies.md for why this form is an alternative, not
// the default, for this plant.
//
// Structurally a twin of AutoChannel (same frame-driven dt, same per-axis
// deadzone handling) - only the last step differs: each axis is scaled onto
// the working zone and written with moveTo(), instead of integrated with
// setVelocity()+Gimbal::update().
class AutoPositionChannel : public IInputChannel
{
public:
    AutoPositionChannel(Gimbal &gimbal,
                         float panKp, float panKi, float panKd,
                         float tiltKp, float tiltKi, float tiltKd,
                         float derivAlpha)
        : _gimbal(gimbal),
          _panPid(panKp, panKi, panKd, config::POS_OUTPUT_MIN, config::POS_OUTPUT_MAX, derivAlpha),
          _tiltPid(tiltKp, tiltKi, tiltKd, config::POS_OUTPUT_MIN, config::POS_OUTPUT_MAX, derivAlpha)
    {
    }

    // A camera ErrorSample arrived off cmd_q - identical contract to
    // AutoChannel::onErrorSample().
    void onErrorSample(bool visible, Point error, uint32_t frameMs)
    {
        _targetVisible = visible;
        if (_targetVisible)
            _error = error;
        _frameMs    = frameMs;
        _frameReady = true;
    }

    // Live retune without disturbing the accumulated integral.
    void setPanGains(float kp, float ki, float kd)  { _panPid.setGains(kp, ki, kd); }
    void setTiltGains(float kp, float ki, float kd) { _tiltPid.setGains(kp, ki, kd); }

    // The gains actually running, straight from each PidPosition - not a
    // separate copy that could drift from what update() uses.
    Gains panGains() const  { return {_panPid.kp(),  _panPid.ki(),  _panPid.kd()};  }
    Gains tiltGains() const { return {_tiltPid.kp(), _tiltPid.ki(), _tiltPid.kd()}; }

    // Full reset: channel switch, tour-done, arm, link-recovered, fault-ack,
    // e-stop, link-stale.
    void reset(uint32_t now) override
    {
        _panPid.reset();
        _tiltPid.reset();
        _frameReady = false;
        _onTarget   = false;
        _lastPidMs  = now;
    }

    Point    error() const    { return _error; }
    bool     onTarget() const { return _onTarget; }
    // PID evaluations since boot (one per fresh frame) - the caller derives Hz.
    uint32_t pidRuns() const  { return _pidRuns; }

    // Drive the gimbal for this tick. `fresh` is the caller's link/frame
    // freshness test - false forces a reset, same as a lost link. Unlike
    // AutoChannel there is nothing to command explicitly to "hold": moveTo()
    // simply isn't called again for an axis, and the servo stays exactly
    // where it was last written.
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

        float dt = (float)(_frameMs - _lastPidMs) / 1000.0f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.5f)   dt = 0.5f;
        _lastPidMs  = _frameMs;
        _frameReady = false;
        ++_pidRuns;

        const ViewPort vp = _gimbal.viewPort();
        const float panDeg  = axisAngle(_panPid, _error.x, dt, vp.center.x, vp.halfWidth(), _gimbal.panAngle());
        const float tiltDeg = axisAngle(_tiltPid, _error.y, dt, vp.center.y, vp.halfHeight(), _gimbal.tiltAngle());
        _gimbal.moveTo(panDeg, tiltDeg);
        updateArrival();
    }

private:
    // Below the deadzone, hold this axis exactly where it is instead of
    // committing to an arbitrary angle inside the servo's own deadband - same
    // reasoning as AutoChannel::axisRate(), just returning a position instead
    // of freezing a rate.
    static float axisAngle(PidPosition &pid, float error, float dt,
                            float center, float halfExtent, float currentAngle)
    {
        const float mag = error < 0.0f ? -error : error;
        if (mag < config::TRACK_DEADZONE)
        {
            pid.hold(error);
            return currentAngle;
        }
        const float unit = pid.update(error, dt); // clamped to [-1, 1]
        return center + unit * halfExtent;
    }

    void updateArrival()
    {
        const float ax = _error.x < 0.0f ? -_error.x : _error.x;
        const float ay = _error.y < 0.0f ? -_error.y : _error.y;
        _onTarget = ax < config::TRACK_DEADZONE && ay < config::TRACK_DEADZONE;
    }

    Gimbal      &_gimbal;
    PidPosition  _panPid;
    PidPosition  _tiltPid;

    Point    _error{0.0f, 0.0f};
    bool     _targetVisible = false;
    bool     _frameReady    = false;
    bool     _onTarget      = false;
    uint32_t _frameMs       = 0;
    uint32_t _lastPidMs     = 0;
    uint32_t _pidRuns       = 0;
};
