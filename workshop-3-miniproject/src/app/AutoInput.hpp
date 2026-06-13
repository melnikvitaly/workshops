#pragma once
#include <cmath>
#include <cstdint>
#include <esp_timer.h>
#include "Input.hpp"
#include "ViewPort.hpp"

// Drop-in, hardware-free input: instead of reading the pot/encoder it drives
// the gimbal through a repeating demo routine, all bounded by the ViewPort:
//
//   1. CIRCLE - one full revolution around the ellipse inscribed in the port
//   2. CROSS  - a diagonal "X" between the port's corners
//
// then loops back to the circle. Same interface (init / poll / read) as
// ManualInput, so main.cpp only changes which type it constructs.
//
// All geometry is computed as a fraction (fx, fy) in [0, 1] of the view port
// and mapped through it, so resizing the port reshapes every phase at once.
class AutoInput : public Input
{
    static constexpr uint32_t DEFAULT_CIRCLE_MS = 6000;
    static constexpr uint32_t DEFAULT_CROSS_MS  = 4000;

    ViewPort _vp;
    int64_t  _circleUs;
    int64_t  _crossUs;
    int64_t  _startUs = 0;

public:
    AutoInput(const ViewPort& vp,
              uint32_t circleMs = DEFAULT_CIRCLE_MS,
              uint32_t crossMs  = DEFAULT_CROSS_MS)
        : _vp(vp),
          _circleUs((int64_t)circleMs * 1000),
          _crossUs((int64_t)crossMs * 1000) {}

    // Anchor the routine to "now" so it starts at the top of the circle phase.
    void init() override { _startUs = esp_timer_get_time(); }

    // Nothing to service: the position is derived from the clock in read().
    void poll() override {}

    // Current target for the elapsed time within the looping routine.
    void read(float& x, float& y) override
    {
        int64_t cycle = _circleUs + _crossUs;
        int64_t t     = (esp_timer_get_time() - _startUs) % cycle;

        float fx, fy;
        if (t < _circleUs)
            circlePoint(t, fx, fy);
        else
            crossPoint(t - _circleUs, fx, fy);

        x = _vp.lerpX(fx);
        y = _vp.lerpY(fy);
    }

private:
    // Point on the unit circle (in [0,1] view-port fractions) for circle time t.
    void circlePoint(int64_t t, float& fx, float& fy) const
    {
        float phase = 2.0f * (float)M_PI * (float)t / (float)_circleUs;
        fx = 0.5f + 0.5f * cosf(phase);
        fy = 0.5f + 0.5f * sinf(phase);
    }

    // Diagonal cross as a closed bowtie through the four corners, so both
    // diagonals get traced: TL -> BR (\) -> BL (bottom) -> TR (/) -> TL (top).
    void crossPoint(int64_t t, float& fx, float& fy) const
    {
        // Corner waypoints as {fx, fy}; the path closes back to the first.
        static constexpr float pts[4][2] = {
            { 0.0f, 1.0f },  // TL
            { 1.0f, 0.0f },  // BR
            { 0.0f, 0.0f },  // BL
            { 1.0f, 1.0f },  // TR
        };
        constexpr int LEGS = 4;

        float legUs = (float)_crossUs / LEGS;
        int   leg   = (int)((float)t / legUs);
        if (leg >= LEGS) leg = LEGS - 1;          // guard the closing boundary
        float u = ((float)t - leg * legUs) / legUs; // 0..1 along this leg

        const float* a = pts[leg];
        const float* b = pts[(leg + 1) % LEGS];
        fx = a[0] + u * (b[0] - a[0]);
        fy = a[1] + u * (b[1] - a[1]);
    }
};
