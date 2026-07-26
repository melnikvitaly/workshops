#pragma once
#include "LogsTarget.hpp"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstdio>

// Times each target's write() and periodically logs the worst (max) time seen
// per target, so a slow sink (e.g. an SD write stalling the collector's task)
// shows up without flooding the console on every single record. Also tallies
// dropped-record notifications (slave ring overflow) the collector reports in
// via addDropped(), on the same interval, so both "how slow are we" and "how
// much are we losing" show up in one periodic line instead of two independent
// cadences.
class TargetTimer
{
public:
    static constexpr int64_t REPORT_INTERVAL_US = 5'000'000;
    static constexpr int     MAX_TRACKED        = 8;

    // Record that the slave reported n more dropped records since we last
    // heard from it (ring overflow). Accumulated into the next periodic
    // report, not logged immediately — the collector still logs each
    // occurrence at WARN level on its own for real-time visibility.
    void addDropped(uint32_t n) { _droppedSinceReport += n; }

    // Writes rec to every target in targets[0..count), timing each write() in
    // microseconds and tracking the worst time per target since the last
    // report. Emits one ESP_LOG summary line at most once every
    // REPORT_INTERVAL_US, then resets the per-target max and dropped tally
    // for the next window.
    void dispatch(LogsTarget* const* targets, int count, const LogRecord& rec)
    {
        if (count > MAX_TRACKED)
            count = MAX_TRACKED;

        for (int i = 0; i < count; ++i)
        {
            int64_t start = esp_timer_get_time();
            targets[i]->write(rec);
            int64_t elapsedUs = esp_timer_get_time() - start;

            _name[i] = targets[i]->name();
            if (elapsedUs > _maxUs[i])
                _maxUs[i] = elapsedUs;
        }
        _trackedCount = count;
        _records++;

        int64_t now = esp_timer_get_time();
        if (now - _lastReportUs < REPORT_INTERVAL_US)
            return;
        _lastReportUs = now;

        char line[192];
        int n = snprintf(line, sizeof(line), "%u records, %u dropped, max write time:",
                         (unsigned)_records, (unsigned)_droppedSinceReport);
        for (int i = 0; i < _trackedCount && n < (int)sizeof(line); ++i)
            n += snprintf(line + n, sizeof(line) - n, " %s=%lldus",
                          _name[i], (long long)_maxUs[i]);
        ESP_LOGI(TAG, "%s", line);

        _records = 0;
        _droppedSinceReport = 0;
        for (int i = 0; i < MAX_TRACKED; ++i)
            _maxUs[i] = 0;
    }

private:
    static constexpr const char* TAG = "TARGET_TIMING";

    const char* _name[MAX_TRACKED]  = {};
    int64_t     _maxUs[MAX_TRACKED] = {};
    int         _trackedCount       = 0;
    uint32_t    _records            = 0;
    uint32_t    _droppedSinceReport = 0;
    int64_t     _lastReportUs       = 0;
};
