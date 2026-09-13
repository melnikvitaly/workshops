#pragma once
#include <cstdint>
#include "Point.hpp"

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
