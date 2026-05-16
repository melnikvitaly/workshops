#pragma once
#include <cstdint>

struct Config
{
    static constexpr uint32_t BAUD_RATE = 115200;
    static constexpr uint32_t LOOP_TRACK_ITERATIONS = 10000;

    static constexpr uint32_t WDT_TIMEOUT_MS = 3'000;     // watchdog resets device if loop stalls
    static constexpr uint32_t HANG_AFTER_MS  = 20'000;    // simulate hang for watchdog demo

    static constexpr uint64_t FAN_PERIOD_US = 8'000'000; // 8 s
    static constexpr uint64_t FAN_ON_US = 4'000'000;     // 4 s
    static constexpr uint64_t FAN_OFF_US = FAN_PERIOD_US - FAN_ON_US;

    static constexpr uint32_t US_PER_MS            = 1000;   // esp_timer_get_time() → ms

    // Tachometer
    static constexpr uint32_t TACH_REPORT_MS      = 1000;   // RPM print interval
    static constexpr uint32_t TACH_PULSES_PER_REV = 2;      // typical fan: 2 pulses/rev
    static constexpr uint32_t MS_PER_MINUTE       = 60'000;

    // Hardware timer (both resolve to 1 µs/tick)
    static constexpr uint32_t TIMER_RESOLUTION_HZ = 1'000'000; // gptimer (Task3)
    static constexpr uint32_t TIMER_CLK_DIVIDER   = 80;        // register divider: 80 MHz / 80 (Task4)
};
