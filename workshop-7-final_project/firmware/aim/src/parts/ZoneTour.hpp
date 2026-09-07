#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Gimbal.hpp"
#include "Point.hpp"

// Boot-time walk around the perimeter of the working zone, with the laser lit.
//
// The zone is a pair of numbers in Config.hpp; this makes it a thing you can
// see. Watching the dot trace the rectangle tells you at a glance where the
// laser can and cannot reach, whether the zone actually covers the scene you
// care about, and - usefully before any tuning - which way each axis moves.
//
// Waypoints are driven positionally via Gimbal::moveTo() rather than through
// setVelocity(): the tour wants to arrive exactly on the corners, and a
// position follower does that without the overshoot an integrator would add.
// The step size is capped per tick, so the motion is still rate limited.
class ZoneTour
{
    static constexpr int MAX_WAYPOINTS = 6;
    static constexpr float ARRIVE_EPS_DEG = 0.5f;

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    Gimbal  &_gimbal;
    float    _rateDegPerSec;
    uint32_t _dwellMs;
    bool     _panAimsRight;  // dot moves right when the pan angle increases
    bool     _tiltAimsDown;  // dot moves down when the tilt angle increases

    Point    _waypoints[MAX_WAYPOINTS];
    int      _count      = 0;
    int      _index      = 0;
    bool     _done       = true;
    bool     _dwelling   = false;
    uint32_t _dwellUntil = 0;

    // Move one axis toward its target by at most `step`.
    static float approach(float current, float target, float step)
    {
        const float delta = target - current;
        if (delta > step)
            return current + step;
        if (delta < -step)
            return current - step;
        return target;
    }

public:
    ZoneTour(Gimbal &gimbal, float rateDegPerSec, uint32_t dwellMs,
             bool panAimsRight, bool tiltAimsDown)
        : _gimbal(gimbal), _rateDegPerSec(rateDegPerSec), _dwellMs(dwellMs),
          _panAimsRight(panAimsRight), _tiltAimsDown(tiltAimsDown) {}

    // Build the perimeter from the gimbal's working zone and start. Must be
    // called after Gimbal::init(), which parks at the zone centre.
    void begin()
    {
        const ViewPort v = _gimbal.viewPort();
        const float loX = v.center.x - v.halfWidth();
        const float hiX = v.center.x + v.halfWidth();
        const float loY = v.center.y - v.halfHeight();
        const float hiY = v.center.y + v.halfHeight();

        // Translate the zone's angle bounds into what an observer facing the
        // scene calls left/right/top/bottom, so the walk below can be expressed
        // visually rather than in raw servo angles.
        const float left   = _panAimsRight ? loX : hiX;
        const float right  = _panAimsRight ? hiX : loX;
        const float top    = _tiltAimsDown ? loY : hiY;
        const float bottom = _tiltAimsDown ? hiY : loY;

        // Clockwise from the top-left corner. The direction is the point: if
        // the dot traces counter-clockwise, one of the two axis-geometry flags
        // in Config.hpp is wrong - and that same wrong flag is what would send
        // the tracking loop running away from the target instead of toward it.
        // Every segment is axis-aligned, which is why per-axis stepping traces
        // clean edges rather than cutting diagonals.
        _waypoints[0] = {left, top};     // start top-left
        _waypoints[1] = {right, top};    // -> right along the top edge
        _waypoints[2] = {right, bottom}; // -> down the right edge
        _waypoints[3] = {left, bottom};  // -> left along the bottom edge
        _waypoints[4] = {left, top};     // -> back up to close the rectangle
        _waypoints[5] = v.center;        // -> park in the middle
        _count        = MAX_WAYPOINTS;

        _index    = 0;
        _done     = false;
        _dwelling = false;
        _gimbal.stop(); // the tour owns the gimbal until it finishes
    }

    bool done() const { return _done; }

    // Current leg, 1-based, for logging.
    int leg() const { return _index + 1; }
    int legs() const { return _count; }

    void update(float dtSec)
    {
        if (_done)
            return;

        if (_dwelling)
        {
            if ((int32_t)(nowMs() - _dwellUntil) < 0)
                return;
            _dwelling = false;
            if (++_index >= _count)
            {
                _done = true;
                return;
            }
        }

        const Point target = _waypoints[_index];
        const float step   = _rateDegPerSec * dtSec;

        const float pan  = approach(_gimbal.panAngle(), target.x, step);
        const float tilt = approach(_gimbal.tiltAngle(), target.y, step);
        _gimbal.moveTo(pan, tilt);

        // Compare against what the gimbal actually accepted: moveTo() clamps,
        // so a waypoint outside the travel limits would otherwise never be
        // "reached" and the tour would stall here forever.
        const float dx = _gimbal.panAngle() - target.x;
        const float dy = _gimbal.tiltAngle() - target.y;
        const bool arrived = (dx > -ARRIVE_EPS_DEG && dx < ARRIVE_EPS_DEG) &&
                             (dy > -ARRIVE_EPS_DEG && dy < ARRIVE_EPS_DEG);

        if (arrived)
        {
            _dwelling   = true;
            _dwellUntil = nowMs() + _dwellMs;
        }
    }
};
