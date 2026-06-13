#pragma once
#include <cstdint>
#include "Point.hpp"

enum class CommandType
{
    Move,
    LaserFlash,
    LaserToggleConstant,
};

struct Command
{
    CommandType type;
    union
    {
        int32_t value;
        Point move;
    };

    static Command action(CommandType type, int32_t value = 0)
    {
        Command c;
        c.type = type;
        c.value = value;
        return c;
    }

    static Command moveTo(Point p)
    {
        Command c;
        c.type = CommandType::Move;
        c.move = p;
        return c;
    }
};
