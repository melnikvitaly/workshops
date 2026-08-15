#pragma once
#include <esp_log.h>
#include <cmath>
#include <cstdio>
#include "Point.hpp"
#include "Config.hpp"
#include "TrackState.hpp"

// Tuning-oriented telemetry. One line per meaningful change, rate limited so
// it cannot flood the shared UART.
//
// With telemetry enabled this is a bare protocol message, rather than ESP_LOGI
// text, so the PC can distinguish it from console output:
//
//   T ex:-0.124 ey:0.058 vpan:-4.9 vtilt:2.1 pan:57.4 tilt:88.2 st:TRACK
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
    void setTelemetry(bool on)
    {
        if (on && !_telemetry)
            _lastMs = 0; // make a newly enabled stream report on the next tick
        _telemetry = on;
    }
    bool telemetry() const     { return _telemetry; }

    void update(uint32_t nowMs,
                Point error, Point velocity,
                float panDeg, float tiltDeg,
                TrackState state)
    {
        const bool stateChanged = !_primed || state != _lastState;

        // Without telemetry the logger is silent except at state transitions -
        // a handful of console lines per session rather than a continuous
        // stream down the UART the PC is trying to send frames on.
        const bool moved = _telemetry &&
                           (fabsf(error.x - _last.x) > _epsilon ||
                            fabsf(error.y - _last.y) > _epsilon);
        const bool heartbeat = _telemetry &&
                               (nowMs - _lastMs >= config::TELEMETRY_HEARTBEAT_MS);

        if (!moved && !stateChanged && !heartbeat)
            return;
        if (!stateChanged && !heartbeat &&
            (nowMs - _lastMs) < config::LOG_MIN_INTERVAL_MS)
            return;

        _last      = error;
        _lastState = state;
        _primed    = true;
        _lastMs    = nowMs;

        if (_telemetry)
        {
            printf("T ex:%+.3f ey:%+.3f vpan:%+.1f vtilt:%+.1f pan:%.1f tilt:%.1f st:%s\n",
                   error.x, error.y, velocity.x, velocity.y, panDeg, tiltDeg,
                   trackStateName(state));
            fflush(stdout);
        }
        else
        {
            ESP_LOGI(_tag, "state %s", trackStateName(state));
        }
    }
};
