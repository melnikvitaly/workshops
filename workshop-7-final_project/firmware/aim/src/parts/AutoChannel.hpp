#pragma once
#include <cmath>
#include <cstdint>
#include "IInputChannel.hpp"
#include "Gimbal.hpp"
#include "Point.hpp"
#include "Pid.hpp"
#include "Config.hpp"

// Camera-tracking input channel: a pan/tilt PID pair closing the loop on the
// error vector reported by EYE, one fresh frame at a time (dt measured
// between frames, not once per tick - ported from ErrorVectorInput::update()).
//
// The setpoint is implicitly zero (see Pid.hpp); this class only carries the
// per-frame bookkeeping (visibility, dt, arrival) around that.
class AutoChannel : public IInputChannel
{
public:
    AutoChannel(Gimbal &gimbal,
                float panKp, float panKi, float panKd, float panMaxSlew,
                float tiltKp, float tiltKi, float tiltKd, float tiltMaxSlew,
                float derivAlpha)
        : _gimbal(gimbal),
          _panPid(panKp, panKi, panKd, -panMaxSlew, panMaxSlew, derivAlpha),
          _tiltPid(tiltKp, tiltKi, tiltKd, -tiltMaxSlew, tiltMaxSlew, derivAlpha)
    {
    }

    // A camera ErrorSample arrived off cmd_q. `error` is only meaningful when
    // `visible` is true - a lost target still advances frameMs so dt stays sane
    // once the target reappears.
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

    // Full reset: channel switch, tour-done, arm, link-recovered, fault-ack,
    // e-stop, link-stale.
    void reset(uint32_t now) override
    {
        resetPids();
        _frameReady = false;
        _onTarget   = false;
        _lastPidMs  = now;
    }

    Point    error() const    { return _error; }
    bool     onTarget() const { return _onTarget; }
    // PID evaluations since boot (one per fresh frame) - the caller derives Hz.
    uint32_t pidRuns() const  { return _pidRuns; }

    // Drive the gimbal for this tick. `fresh` is the caller's link/frame
    // freshness test (ctrl already computes it for the FSM) - false forces a
    // reset and holds the gimbal still, same as a lost link.
    void update(uint32_t now, bool fresh) override
    {
        if (!fresh)
        {
            reset(now);
            _gimbal.setVelocity({0.0f, 0.0f});
            return;
        }
        if (!_targetVisible)
        {
            _gimbal.setVelocity({0.0f, 0.0f});
            _lastPidMs = now;
            return;
        }
        if (!_frameReady)
            return; // nothing new - the gimbal holds the last commanded rate

        float dt = (float)(_frameMs - _lastPidMs) / 1000.0f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.5f)   dt = 0.5f;
        _lastPidMs  = _frameMs;
        _frameReady = false;
        ++_pidRuns;

        _gimbal.setVelocity({axisRate(_panPid, _error.x, dt),
                             axisRate(_tiltPid, _error.y, dt)});
        updateArrival();
    }

private:
    void resetPids()
    {
        _panPid.reset();
        _tiltPid.reset();
    }

    static float axisRate(Pid &pid, float error, float dt)
    {
        const float mag = error < 0.0f ? -error : error;
        if (mag < config::TRACK_DEADZONE)
        {
            pid.hold(error); // arrived: freeze, do not just zero the error
            return 0.0f;
        }
        return pid.update(error, dt);
    }

    void updateArrival()
    {
        const float ax = _error.x < 0.0f ? -_error.x : _error.x;
        const float ay = _error.y < 0.0f ? -_error.y : _error.y;
        _onTarget = ax < config::TRACK_DEADZONE && ay < config::TRACK_DEADZONE;
    }

    Gimbal &_gimbal;
    Pid     _panPid;
    Pid     _tiltPid;

    Point    _error{0.0f, 0.0f};
    bool     _targetVisible = false;
    bool     _frameReady    = false;
    bool     _onTarget      = false;
    uint32_t _frameMs       = 0;
    uint32_t _lastPidMs     = 0;
    uint32_t _pidRuns       = 0;
};
