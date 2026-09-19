#pragma once
#include "Point.hpp"
#include "Zone.hpp"

// The gimbal's working area, expressed as a rectangle in *servo-angle* space
// (degrees): a centre angle per axis plus the full pan/tilt extents it spans.
//
// Every input just produces a unit point: each axis in [-1, 1] with (0,0) at
// the centre, -1/+1 at the edges of the working area. translate() maps such a
// unit point into absolute servo angles, scaling each axis by its reach from
// the centre and clamping to the rectangle. So inputs stay coordinate-agnostic
// (pure [-1, 1]) and the single question of *where* the centre sits and how far
// it reaches lives only here, directly in the angles the servos understand.
struct ViewPort
{
    Point center;            // rectangle centre in servo angles (degrees)
    float width, height;     // full extents (degrees; half* is the reach from centre)

    // Build from absolute pan/tilt angle bounds (degrees), e.g. config values.
    static constexpr ViewPort fromBounds(float panMin, float panMax,
                                         float tiltMin, float tiltMax)
    {
        return { { 0.5f * (panMin + panMax), 0.5f * (tiltMin + tiltMax) },
                 panMax - panMin,            tiltMax - tiltMin };
    }

    // Build from a Zone (min/max bounds), e.g. the working zone read from config.
    static constexpr ViewPort fromZone(Zone z)
    {
        return fromBounds(z.panMin, z.panMax, z.tiltMin, z.tiltMax);
    }

    float halfWidth()  const { return 0.5f * width; }
    float halfHeight() const { return 0.5f * height; }

    // The same rectangle, expressed as per-axis min/max bounds instead of
    // centre + extents.
    Zone toZone() const
    {
        return {center.x - halfWidth(), center.x + halfWidth(),
                center.y - halfHeight(), center.y + halfHeight()};
    }

    // Map a unit point (each axis in [-1, 1], origin at the centre) into
    // absolute servo angles: clamp to [-1, 1], scale by the per-axis reach and
    // add the centre.
    Point translate(Point unit) const
    {
        return { translateX(-unit.x), translateY(-unit.y) };
    }

    float translateX(float u) const
    {
        if (u < -1.0f) u = -1.0f; else if (u > 1.0f) u = 1.0f;
        return center.x + u * halfWidth();
    }
    float translateY(float u) const
    {
        if (u < -1.0f) u = -1.0f; else if (u > 1.0f) u = 1.0f;
        return center.y + u * halfHeight();
    }
};
