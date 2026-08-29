#pragma once
#include <cstdint>
#include <esp_timer.h>

// Arduino's millis() has no ESP-IDF equivalent; esp_timer is the closest
// monotonic source. Wraps after ~49 days, exactly like millis(), so all
// comparisons below stay of the "now - startedAt >= duration" form.
inline uint32_t nowMs()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
