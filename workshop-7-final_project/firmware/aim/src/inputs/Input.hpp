#pragma once
#include "Command.hpp"
#include "Queue.hpp"
#include "InputEvent.hpp"

class Input
{
    Queue<Command> _commands;

protected:
    void enqueue(Command c) { _commands.push(c); }

public:
    virtual ~Input() = default;

    // Human-readable name of this source (for logs / on-screen feedback).
    virtual const char *name() const = 0;

    virtual void init() = 0;

    // Service time-critical hardware; called from the fast loop.
    virtual void tick() = 0;

    // Called once per update before the controller drains the queue. Each source
    // places whatever Commands it wants executed this cycle into the queue here:
    // typically the latest continuous Move target, plus any source-specific
    // actions (e.g. an automated laser shot at a point of interest).
    virtual void update() = 0;

    virtual void onEvent(const InputEvent &e)
    {
        switch (e.type)
        {
        case InputEventType::ButtonClick:
            enqueue(Command::action(CommandType::LaserFire));
            break;
        case InputEventType::ButtonLongPress:
            enqueue(Command::action(CommandType::LaserToggleConstant));
            break;
        case InputEventType::ToggleArm:
            break; // only meaningful to sources that close a loop
        }
    }

    bool nextCommand(Command &out) { return _commands.pop(out); }
};
