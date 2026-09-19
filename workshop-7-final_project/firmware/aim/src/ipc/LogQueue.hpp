#pragma once
#include <cstdint>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "StateMachine.hpp"

// log_q record - the CSV row logger writes to SD, plus a kind so the
// logger can also carry boot markers and transition events. Phase 0 is
// monotonic only: t_wall_iso is written empty, never a placeholder epoch.
struct LogRecord
{
    enum class Kind : uint8_t { Sample, Boot, Transition, Sd };

    Kind     kind;
    uint32_t seq;
    uint64_t t_mono_us;
    State    state;
    uint8_t  channel;
    float    ex, ey, vpan, vtilt, pan, tilt;
    uint32_t flags;
    char     note[24]; // reset reason / transition trigger / sd condition
};

// log_q back-pressure policy: drop-oldest with a counter, never block a producer.
// ctrl and link_uart use this.
inline void logSend(QueueHandle_t q, const LogRecord *rec, std::atomic<uint32_t> *dropped)
{
    if (xQueueSend(q, rec, 0) == pdTRUE)
        return;
    LogRecord scratch;
    if (xQueueReceive(q, &scratch, 0) == pdTRUE)
        dropped->fetch_add(1);
    xQueueSend(q, rec, 0);
}
