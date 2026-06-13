#pragma once
#include <cstdint>

// A discrete event raised on an Input via onEvent(). Today the single shared
// control button raises these, but the name is deliberately broad (InputEvent,
// not ButtonEvent) so other sources can raise their own kinds later -- the
// active source switches on the type and decides what each one means.
enum class InputEventType
{
    ButtonClick,      // short press / release
    ButtonLongPress,  // press held past the long-press threshold
    // future: ButtonPress, Rotate, ValueChange, ...
};

// Tag + optional payload. `value` carries event-specific data (e.g. a long-press
// hold duration in ms, or a rotation delta) and is 0 when unused.
struct InputEvent
{
    InputEventType type;
    int32_t        value = 0;
};
