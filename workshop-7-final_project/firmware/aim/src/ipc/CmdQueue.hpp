#pragma once
#include <cstdint>
#include "Point.hpp"

// cmd_q item - one queue, tagged union, many producers (link_uart, ui) and one
// consumer (ctrl). Producers never see each other.
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
    MoveTo,         // vec = (pan, tilt) deg - absolute position, a P frame
    FireLaser,      // request one blank pulse (granted only if safety agrees)
    Arm,            // DISARMED/PARKED -> ARMED (CONTROL button, `arm` command)
    Disarm,         // ARMED/LINK_LOST -> DISARMED (CONTROL button, `disarm` command)
    FaultAck,       // clear the latched FAULT (CONTROL button or cfg.set fault.ack)
    ZoneTourStart,  // re-enter ZONE_TOUR on demand (cfg.set control.zone_tour)
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
