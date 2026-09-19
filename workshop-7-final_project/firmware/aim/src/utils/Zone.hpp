#pragma once
#include "Point.hpp"

// Per-axis angle bounds (degrees): pan/tilt min and max. Used both for the
// working zone read from config and, inside Gimbal, for the hard mechanical
// limits and the effective travel derived from them - anywhere a per-axis
// [lo, hi] pair is needed rather than a centre-based ViewPort.
// A plain aggregate with equality so ctrl can detect a config-plane change in
// one comparison instead of four.
struct Zone
{
    float panMin, panMax, tiltMin, tiltMax;

    bool operator==(const Zone &o) const
    {
        return panMin == o.panMin && panMax == o.panMax &&
               tiltMin == o.tiltMin && tiltMax == o.tiltMax;
    }
    bool operator!=(const Zone &o) const { return !(*this == o); }

    // Clamp a (pan, tilt) point in degrees into this zone.
    Point clamp(Point deg) const
    {
        return {clampf(deg.x, panMin, panMax), clampf(deg.y, tiltMin, tiltMax)};
    }

    // True if a (pan, tilt) point sits on (or past) either edge of this zone.
    bool atLimit(Point deg) const
    {
        return deg.x <= panMin || deg.x >= panMax ||
               deg.y <= tiltMin || deg.y >= tiltMax;
    }

    // Narrowest zone that fits inside both a and b.
    static Zone intersect(const Zone &a, const Zone &b)
    {
        return {maxf(a.panMin, b.panMin),   minf(a.panMax, b.panMax),
                maxf(a.tiltMin, b.tiltMin), minf(a.tiltMax, b.tiltMax)};
    }

private:
    static float clampf(float v, float lo, float hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
    static float maxf(float a, float b) { return a > b ? a : b; }
    static float minf(float a, float b) { return a < b ? a : b; }
};
