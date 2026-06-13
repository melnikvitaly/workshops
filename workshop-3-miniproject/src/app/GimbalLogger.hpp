#pragma once
#include <esp_log.h>
#include <cmath>

// Console telemetry for the gimbal. Keeps its own "last logged" state and only
// emits a line when the (x,y) target moved past `epsilon` or the laser toggled,
// so the main loop stays free of logging noise.
class GimbalLogger
{
    const char *_tag;
    float       _epsilon;
    float       _lastX     = 2.0f;   // out-of-range sentinel -> first call logs
    float       _lastY     = 2.0f;
    bool        _lastLaser = false;

public:
    GimbalLogger(const char *tag, float epsilon) : _tag(tag), _epsilon(epsilon) {}

    void update(float x, float y, float panDeg, float tiltDeg, bool laserOn)
    {
        bool changed = fabsf(x - _lastX) > _epsilon ||
                       fabsf(y - _lastY) > _epsilon ||
                       laserOn != _lastLaser;
        if (!changed) return;

        _lastX     = x;
        _lastY     = y;
        _lastLaser = laserOn;
        ESP_LOGI(_tag, "x=%+.2f y=%+.2f  pan=%.0f tilt=%.0f%s",
                 x, y, panDeg, tiltDeg, laserOn ? "  [LASER]" : "");
    }
};
