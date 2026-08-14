#pragma once
#include <esp_log.h>
#include <cmath>
#include "Point.hpp"
#include "Config.hpp"
#include "TrackState.hpp"

// Tuning-oriented log. One line per meaningful change, rate limited so it
// cannot flood the console - which matters more than usual here, because the
// console shares UART0 with the incoming error stream.
//
// The format is deliberately plottable (Teleplot / serial-plotter style):
//
//   ex:-0.124 ey:0.058 vpan:-4.9 vtilt:2.1 pan:57.4 tilt:88.2 st:TRACK
//
// ex/ey are the measured error (the process variable; the setpoint is always
// zero), vpan/vtilt the PID rate output, pan/tilt the resulting angles. Watch
// ex settle to zero after a step to judge overshoot and settling time.
class AppLogger
{
    const char *_tag;
    float       _epsilon;
    Point       _last      = {2.0f, 2.0f}; // out-of-range sentinel -> first call logs
    TrackState  _lastState = TrackState::Disarmed;
    bool        _primed    = false;
    uint32_t    _lastMs    = 0;
    bool        _telemetry = config::LOG_TELEMETRY;

public:
    AppLogger(const char *tag, float epsilon) : _tag(tag), _epsilon(epsilon) {}

    // Runtime toggle (the 'T' frame), so a tuning run does not need a reflash
    // and the port goes quiet again the moment you are done.
    void setTelemetry(bool on) { _telemetry = on; }
    bool telemetry() const     { return _telemetry; }

    void update(uint32_t nowMs,
                Point error, Point velocity,
                float panDeg, float tiltDeg,
                TrackState state)
    {
        const bool stateChanged = !_primed || state != _lastState;

        // Without telemetry the logger is silent except at state transitions -
        // a handful of lines per session rather than a continuous stream down
        // the UART the PC is trying to send frames on.
        const bool moved = _telemetry &&
                           (fabsf(error.x - _last.x) > _epsilon ||
                            fabsf(error.y - _last.y) > _epsilon);

        if (!moved && !stateChanged)
            return;
        if (!stateChanged && (nowMs - _lastMs) < config::LOG_MIN_INTERVAL_MS)
            return;

        _last      = error;
        _lastState = state;
        _primed    = true;
        _lastMs    = nowMs;

        ESP_LOGI(_tag, "ex:%+.3f ey:%+.3f vpan:%+.1f vtilt:%+.1f pan:%.1f tilt:%.1f st:%s",
                 error.x, error.y, velocity.x, velocity.y, panDeg, tiltDeg,
                 trackStateName(state));
    }
};
