#pragma once
#include <cstdint>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Point.hpp"
#include "StateMachine.hpp"

// cmd_q item - one queue, tagged union, many producers (link_uart, ui) and one
// consumer (ctrl). Producers never see each other (docs/architecture.md §2).
// Fixed-width fields only: copied by value through a FreeRTOS queue.
//
// Live gains, telemetry on/off, channel selection and the `Q` reply are NOT
// carried here: gains and channel are config-plane state that ctrl picks up via
// its per-step snapshot, and telemetry / query are answered inside link_uart.

enum class CmdKind : uint8_t
{
    ErrorSample,    // vec = raw (dx, dy) from an E frame; i = valid flag. AUTO channel.
    ManualVelocity, // vec = (vpan, vtilt) deg/s from an M frame.          MANUAL channel.
    Nudge,          // vec = (dpan, dtilt) deg - open-loop disturbance
    FireLaser,      // request one blank pulse (granted only if safety agrees)
    Arm,            // toggle DISARMED <-> ARMED (CONTROL button)
    FaultAck,       // clear the latched FAULT (CONTROL button or cfg.set fault.ack)
};

struct CmdItem
{
    CmdKind  kind;
    bool     flag; // ErrorSample: target-visible
    uint32_t t_ms; // xTaskGetTickCount() ms at enqueue - ctrl uses it for the PID dt
    union
    {
        Point   vec;
        int32_t i;
    };
};

// log_q record - the CSV row of docs/architecture.md §5, plus a kind so the
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

// log_q back-pressure policy: drop-oldest with a counter, never block a producer
// (docs/architecture.md §5, docs/coding.md). ctrl and link_uart use this.
inline void logSend(QueueHandle_t q, const LogRecord *rec, std::atomic<uint32_t> *dropped)
{
    if (xQueueSend(q, rec, 0) == pdTRUE)
        return;
    LogRecord scratch;
    if (xQueueReceive(q, &scratch, 0) == pdTRUE)
        dropped->fetch_add(1, std::memory_order_relaxed);
    xQueueSend(q, rec, 0);
}
