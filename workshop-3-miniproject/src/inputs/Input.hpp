#pragma once
#include "Command.hpp"
#include "Queue.hpp"
#include "InputEvent.hpp"

// Common base for every target source (ManualInput, AutoInput, JoystickInput,
// ...). main.cpp keeps a list of these and the on-board button cycles which one
// is active, so the loop only ever talks to an Input* and never cares which
// concrete source is driving the gimbal.
//
// Sources are FULLY responsible for the commands sent on to the gimbal/laser.
// The base provides the plumbing: a command queue that a source fills via
// enqueue() (from its own logic or from the control-button handlers below) and
// the caller drains via nextCommand().
//
// The single physical control button lives in main.cpp; its events are routed
// to whichever source is active via the onButton* handlers. The default maps
// them to laser control, but a source may override to give the button a
// source-specific meaning (or ignore it) -- so one button, no duplicated wiring,
// yet each source still decides what its clicks do.
class Input
{
    Queue<Command> _commands;

protected:
    // Queue a command for the caller to carry out (drained via nextCommand()).
    void enqueue(Command c) { _commands.push(c); }

public:
    virtual ~Input() = default;

    // Human-readable name of this source (for logs / on-screen feedback).
    virtual const char *name() const = 0;

    // Configure this source's hardware. Safe to call once per source even when
    // several share a peripheral (see ADC::init).
    virtual void init() = 0;

    // Service time-critical hardware; called from the fast loop.
    virtual void tick() = 0;

    // Latest continuous target, as a Move command in unit coordinates: each
    // axis in [-1, 1] with (0,0) at the centre and ±1 at the working-area edges.
    // It is a "latest value" -- sampled each cycle, never queued -- so the caller
    // reads it directly rather than through nextCommand(). The gimbal's ViewPort
    // turns the unit target into absolute servo angles.
    virtual Command currentTarget() = 0;

    // A discrete input event, routed from the single shared button (and any
    // future event source) to whichever source is active. Default maps the
    // button events to laser control; override to give an event a source-specific
    // meaning, and ignore the types this source doesn't care about.
    virtual void onEvent(const InputEvent& e)
    {
        switch (e.type)
        {
            case InputEventType::ButtonClick:     enqueue(Command::action(CommandType::LaserFlash));          break;
            case InputEventType::ButtonLongPress: enqueue(Command::action(CommandType::LaserToggleConstant)); break;
        }
    }

    // Pop the oldest queued command into out; false (out untouched) if none.
    bool nextCommand(Command& out) { return _commands.pop(out); }
};
