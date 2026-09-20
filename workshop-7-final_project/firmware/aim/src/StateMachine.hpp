#pragma once
#include <cstdint>
#include <atomic>

// The AIM control FSM.
//
//   BOOT -> SELFTEST -> DISARMED -> ARMED
//
// ZONE_TOUR sits between SELFTEST and DISARMED only when the `boot.tour` config
// key is on; it can also be entered on demand from DISARMED / PARKED.
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

// The state half of the laser interlock. safety adds the
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

    StateMachine(std::atomic<State> &state, TransitionFn fn, void *ctx)
        : _state(state), _fn(fn), _ctx(ctx)
    {
        _state.store(State::Boot);
    }

    State state() const { return _state.load(); }

    // No-op if already there. `trigger` is a short stable token: "boot",
    // "selftest.ok", "tour.done", "btn.control", "link.stale", "link.fresh",
    // "estop", "fault.ack", "idle", "btn.mode", "cfg.channel", "cfg.tour".
    void set(State to, const char *trigger)
    {
        const State from = _state.load();
        if (to == from)
            return;
        _state.store(to);
        if (_fn)
            _fn(_ctx, from, to, trigger);
    }

private:
    std::atomic<State> &_state;
    TransitionFn        _fn;
    void               *_ctx;
};
