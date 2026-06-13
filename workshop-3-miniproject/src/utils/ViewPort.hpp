#pragma once

// A rectangular region of the gimbal's normalised (x, y) target space (the
// full travel is [-1..1] on each axis). Every input maps/clamps its output
// into this rectangle, so the working area can be restricted in one place
// without changing the gimbal or any individual input's logic.
//
// The default full() rectangle is the whole [-1,1] square, i.e. no restriction.
struct ViewPort
{
    float xMin, xMax;
    float yMin, yMax;

    // The whole normalised square: the entire travel range.
    static constexpr ViewPort full() { return { -1.0f, 1.0f, -1.0f, 1.0f }; }

    float width()      const { return xMax - xMin; }
    float height()     const { return yMax - yMin; }
    float centerX()    const { return 0.5f * (xMin + xMax); }
    float centerY()    const { return 0.5f * (yMin + yMax); }
    float halfWidth()  const { return 0.5f * width(); }
    float halfHeight() const { return 0.5f * height(); }

    // Map t in [0, 1] across the rectangle's axis (0 -> min edge, 1 -> max edge).
    float lerpX(float t) const { return xMin + t * width(); }
    float lerpY(float t) const { return yMin + t * height(); }

    float clampX(float x) const { return x < xMin ? xMin : (x > xMax ? xMax : x); }
    float clampY(float y) const { return y < yMin ? yMin : (y > yMax ? yMax : y); }
};
