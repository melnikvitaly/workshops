#pragma once
#include <esp_log.h>
#include <cmath>
#include "Point.hpp"

class AppLogger
{
    const char *_tag;
    float       _epsilon;
    Point       _last      = { 2.0f, 2.0f };  // out-of-range sentinel -> first call logs
    bool        _lastLaser = false;

public:
    AppLogger(const char *tag, float epsilon) : _tag(tag), _epsilon(epsilon) {}

    void update(Point target, float panDeg, float tiltDeg, bool laserOn)
    {
        bool changed = fabsf(target.x - _last.x) > _epsilon ||
                       fabsf(target.y - _last.y) > _epsilon ||
                       laserOn != _lastLaser;
        if (!changed) return;

        _last      = target;
        _lastLaser = laserOn;
        ESP_LOGI(_tag, "x=%+.2f y=%+.2f  pan=%.0f tilt=%.0f%s",
                 target.x, target.y, panDeg, tiltDeg, laserOn ? "  [LASER]" : "");
    }
};
