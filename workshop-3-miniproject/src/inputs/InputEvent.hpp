#pragma once
#include <cstdint>

enum class InputEventType
{
    ButtonClick,
    ButtonLongPress,
};

struct InputEvent
{
    InputEventType type;
    int32_t value = 0;
};
