#pragma once
#include <cstdint>

// Strategy interface for the exclusive control-loop input source. ctrl selects
// one implementation per config::Channel and calls it once a tick while
// ARMED; each decides how to drive the Gimbal it was constructed with.
class IInputChannel
{
public:
    virtual ~IInputChannel() = default;

    // Drive the gimbal for this tick. `fresh` is ctrl's link/frame-freshness
    // test - the Auto-family channels act on it, the others ignore it.
    virtual void update(uint32_t now, bool fresh) = 0;

    // Drop accumulated state: channel switch, arm, tour-done, fault-ack,
    // link-recovered, e-stop, link-stale. Each channel decides what "reset"
    // means for itself (e.g. an Auto-family channel also drops its PID state).
    virtual void reset(uint32_t now) = 0;
};
