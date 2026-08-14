#pragma once
#include <cstdint>

enum class InputEventType
{
    ButtonClick,
    ButtonLongPress,
    ToggleArm, // enable/disable closed-loop motion
};

struct InputEvent
{
    InputEventType type;
    int32_t value = 0;
};
