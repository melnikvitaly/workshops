#pragma once
#include <Arduino.h>

struct Config
{
    static constexpr unsigned long BLINK_TIME_MS = 100;
    static constexpr uint8_t DEBOUNCE_MS = 20;
    static constexpr uint16_t LONG_PRESS_MS = 3000;
    static constexpr uint32_t LOOP_TRACK_ITERATIONS = 100000;
};
