#pragma once
#include <cstdint>
#include <atomic>

// The AIM control FSM (docs/architecture.md §2).
//
//   BOOT -> SELFTEST -> ZONE_TOUR -> DISARMED -> ARMED
//
// plus PARKED (idle: servos detached, laser off), LINK_LOST (the selected
// channel went stale) and a latched FAULT that only an explicit operator
// acknowledgement clears.
//
// The machine is owned and mutated by the ctrl task alone. Every other task
// reads the state through the std::atomic handed to the constructor. Each
// transition is reported to a callback, which turns it into an `evt` line and a
// log record - "every transition logged with its trigger".
enum class State : uint8_t
{
    Boot,
    SelfTest,
    ZoneTour,
    Disarmed,
    Armed,
    Parked,
    LinkLost,
    Fault,
};

inline const char *stateName(State s)
{
    switch (s)
    {
    case State::Boot:     return "BOOT";
    case State::SelfTest: return "SELFTEST";
    case State::ZoneTour: return "ZONE_TOUR";
    case State::Disarmed: return "DISARMED";
    case State::Armed:    return "ARMED";
    case State::Parked:   return "PARKED";
    case State::LinkLost: return "LINK_LOST";
    case State::Fault:    return "FAULT";
    }
    return "?";
}

// The state half of the laser interlock (docs/interfaces.md §7). safety adds the
// link-fresh, no-E-stop and WDT-healthy terms. The beam is forced off in BOOT,
// SELFTEST, DISARMED, LINK_LOST, PARKED and FAULT.
inline bool stateAllowsLaser(State s)
{
    return s == State::ZoneTour || s == State::Armed;
}

class StateMachine
{
public:
    using TransitionFn = void (*)(void *ctx, State from, State to, const char *trigger);

    StateMachine(std::atomic<State> &published, TransitionFn fn, void *ctx)
        : _published(published), _fn(fn), _ctx(ctx)
    {
        _published.store(_state, std::memory_order_relaxed);
    }

    State state() const { return _state; }
    bool  laserAllowed() const { return stateAllowsLaser(_state); }

    // No-op if already there. `trigger` is a short stable token
    // (docs/protocol.md §3.4): "boot", "selftest.ok", "tour.done",
    // "btn.control", "link.stale", "link.fresh", "estop", "fault.ack", "idle",
    // "btn.mode", "cfg.channel".
    void set(State to, const char *trigger)
    {
        if (to == _state)
            return;
        const State from = _state;
        _state = to;
        _published.store(to, std::memory_order_relaxed);
        if (_fn)
            _fn(_ctx, from, to, trigger);
    }

private:
    std::atomic<State> &_published;
    TransitionFn        _fn;
    void               *_ctx;
    State               _state = State::Boot;
};
