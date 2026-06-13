#pragma once

// A 2D coordinate pair in the normalised target space (x = pan / horizontal
// axis, y = tilt / vertical axis). A plain aggregate so it stays trivially
// copyable and safe to put in a union (see Command).
struct Point
{
    float x, y;
};
