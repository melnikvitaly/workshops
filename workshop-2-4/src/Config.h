#pragma once
#include <cstdint>

struct Config
{
    static constexpr uint32_t BAUD_RATE = 115200;
    static constexpr uint32_t LOOP_TRACK_ITERATIONS = 10000;
    static constexpr uint8_t DEBOUNCE_MS = 50;
    static constexpr uint8_t POLL_INTERVAL_MS = 5;
    static constexpr uint16_t LONG_PRESS_MS = 3000;
    static constexpr unsigned long ALERT_DURATION_MS = 5000;
    static constexpr unsigned long BLINK_DURATION_MS = 3000;
    static constexpr unsigned long BLINK_INTERVAL_MS = 200;
};
