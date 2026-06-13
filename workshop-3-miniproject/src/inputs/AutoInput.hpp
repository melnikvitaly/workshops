#pragma once
#include <cmath>
#include <cstdint>
#include <esp_timer.h>
#include "Input.hpp"

// Drop-in, hardware-free input: instead of reading the pot/encoder it drives
// the gimbal through a repeating demo routine, spanning the whole working area:
//
//   1. CIRCLE - one full revolution around the ellipse inscribed in the area
//   2. CROSS  - a diagonal "X" between the area's corners
//
class AutoInput : public Input
{
    static constexpr uint32_t DEFAULT_CIRCLE_MS = 6000;
    static constexpr uint32_t DEFAULT_CROSS_MS  = 4000;

    int64_t  _circleUs;
    int64_t  _crossUs;
    int64_t  _startUs = 0;

public:
    AutoInput(uint32_t circleMs = DEFAULT_CIRCLE_MS,
              uint32_t crossMs  = DEFAULT_CROSS_MS)
        : _circleUs((int64_t)circleMs * 1000),
          _crossUs((int64_t)crossMs * 1000) {}

    const char *name() const override { return "auto"; }
    // Anchor the routine to "now" so it starts at the top of the circle phase.
    void init() override { _startUs = esp_timer_get_time(); }
    // Position is derived from the clock in read(); nothing to poll.
    void tick() override {}

    // Current target (unit coords in [-1, 1]) for the elapsed time in the loop.
    Command currentTarget() override
    {
        int64_t cycle = _circleUs + _crossUs;
        int64_t t     = (esp_timer_get_time() - _startUs) % cycle;

        Point u = (t < _circleUs) ? circlePoint(t)
                                  : crossPoint(t - _circleUs);

        // TODO: could enqueue(Command::action(CommandType::LaserFlash)) at a point of interest.
        return Command::moveTo(u);
    }

private:
    // Point on the unit circle (centred unit coords in [-1, 1]) for circle time t.
    Point circlePoint(int64_t t) const
    {
        float phase = 2.0f * (float)M_PI * (float)t / (float)_circleUs;
        return { cosf(phase), sinf(phase) };
    }

    // Diagonal cross as a closed bowtie through the four corners, so both
    // diagonals get traced: TL -> BR (\) -> BL (bottom) -> TR (/) -> TL (top).
    Point crossPoint(int64_t t) const
    {
        // Corner waypoints as centred {ux, uy}; the path closes back to the first.
        static constexpr float pts[4][2] = {
            { -1.0f,  1.0f },  // TL
            {  1.0f, -1.0f },  // BR
            { -1.0f, -1.0f },  // BL
            {  1.0f,  1.0f },  // TR
        };
        constexpr int LEGS = 4;

        float legUs = (float)_crossUs / LEGS;
        int   leg   = (int)((float)t / legUs);
        if (leg >= LEGS) leg = LEGS - 1;          // guard the closing boundary
        float u = ((float)t - leg * legUs) / legUs; // 0..1 along this leg

        const float* a = pts[leg];
        const float* b = pts[(leg + 1) % LEGS];
        return { a[0] + u * (b[0] - a[0]),
                 a[1] + u * (b[1] - a[1]) };
    }
};
