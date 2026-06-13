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

    // Latest continuous target
    virtual Command currentTarget() = 0;

    virtual void onEvent(const InputEvent &e)
    {
        switch (e.type)
        {
        case InputEventType::ButtonClick:
            enqueue(Command::action(CommandType::LaserFlash));
            break;
        case InputEventType::ButtonLongPress:
            enqueue(Command::action(CommandType::LaserToggleConstant));
            break;
        }
    }

    bool nextCommand(Command &out) { return _commands.pop(out); }
};
