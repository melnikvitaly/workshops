#pragma once
#include <cstdint>
#include "IInputChannel.hpp"
#include "Gimbal.hpp"
#include "Point.hpp"
#include "Config.hpp"

// Manual-velocity input channel: holds the last commanded rate and drives the
// gimbal with it until it goes stale (no ManualVelocity command for
// config::TRACK_TIMEOUT_MS) - the same failsafe shape as the auto channel's
// link timeout, just fed by joystick/UI commands instead of camera frames.
class ManualChannel : public IInputChannel
{
public:
    explicit ManualChannel(Gimbal &gimbal) : _gimbal(gimbal) {}

    // A ManualVelocity command arrived off cmd_q.
    void set(Point v, uint32_t nowMs)
    {
        _vel    = v;
        _lastMs = nowMs;
    }

    // Back to neutral: channel switch, arm/disarm, tour-done, fault-ack.
    void reset(uint32_t /*now*/) override { _vel = {0.0f, 0.0f}; }

    // Drive the gimbal for this tick. `fresh` is the auto channel's concern.
    void update(uint32_t now, bool /*fresh*/) override
    {
        _gimbal.setVelocity((now - _lastMs > config::TRACK_TIMEOUT_MS)
                                 ? Point{0.0f, 0.0f}
                                 : _vel);
    }

private:
    Gimbal  &_gimbal;
    Point    _vel{0.0f, 0.0f};
    uint32_t _lastMs = 0;
};
