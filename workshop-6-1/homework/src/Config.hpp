#pragma once
#include <cstdint>

// Behaviour of the alert sequence — identical to workshop 2-3.
struct Config
{
    static constexpr uint32_t DEBOUNCE_MS       = 20;
    static constexpr uint32_t LONG_PRESS_MS     = 3000;
    static constexpr uint32_t ALERT_DURATION_MS = 5000;
    static constexpr uint32_t BLINK_DURATION_MS = 3000;
    static constexpr uint32_t BLINK_INTERVAL_MS = 200;

    // Task periods. In 2-3 every tick() ran at whatever rate the superloop
    // happened to spin at; here each task picks the rate it actually needs.
    static constexpr uint32_t BUTTON_PERIOD_MS = 5;
    static constexpr uint32_t ALERT_PERIOD_MS  = 10;
    static constexpr uint32_t POT_PERIOD_MS    = 50;
    static constexpr uint32_t STATS_PERIOD_MS  = 5000;
};
