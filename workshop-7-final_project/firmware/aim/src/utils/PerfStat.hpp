#pragma once
#include <cstdint>
#include <esp_timer.h>
#include "Ema.hpp"

// Running min / max / EWMA of a step duration in microseconds. Single
// writer, lossy reader - the same no-lock rule as Ipc::TelemSample and
// Ipc::Sd: telemetry only, never a control input.
struct PerfStat
{
    uint32_t   minUs = UINT32_MAX;
    uint32_t   maxUs = 0;
    Ema<float> ema{0.2f};

    void sample(int64_t us)
    {
        const uint32_t v = us < 0 ? 0
                          : us > (int64_t)UINT32_MAX ? UINT32_MAX
                                                      : (uint32_t)us;
        if (v < minUs) minUs = v;
        if (v > maxUs) maxUs = v;
        ema.update((float)v);
    }

    // 0 rather than UINT32_MAX before the first sample - a wire-friendly rest
    // value, since "never sampled" (e.g. the OLED absent, render() never ran)
    // is not itself an error worth encoding.
    uint32_t minUsOrZero() const { return minUs == UINT32_MAX ? 0 : minUs; }
    uint32_t emaUs() const { return (uint32_t)ema.value(); }
};

// RAII span timer: samples the esp_timer_get_time() delta across its scope
// into a PerfStat on destruction. Task-context only - PerfStat is telemetry,
// never touched from an ISR.
class ScopedPerf
{
    PerfStat &_stat;
    int64_t   _t0;

public:
    explicit ScopedPerf(PerfStat &stat) : _stat(stat), _t0(esp_timer_get_time()) {}
    ~ScopedPerf() { _stat.sample(esp_timer_get_time() - _t0); }
};
