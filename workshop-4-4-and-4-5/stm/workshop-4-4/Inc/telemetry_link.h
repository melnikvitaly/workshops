#ifndef TELEMETRY_LINK_H
#define TELEMETRY_LINK_H

// SPI-master telemetry link to the ESP32-C3.
//
// This board is the SPI **master** and the only talker on the link: once a
// second it snapshots the sensors published by sensor_stream.c, packs them into
// one fixed 32-byte frame (protocol/telemetry_packet.h — the same header the
// ESP32 compiles) and clocks it out with a single blocking HAL_SPI_Transmit.
// The ESP32-C3 is the slave and only receives.
//
// Synchronous by requirement, and the code shows it: no DMA, no interrupts, no
// ring buffer, no callbacks. CS goes low, 32 bytes are shifted out, CS goes
// high, and the function returns having either sent the whole frame or reported
// the HAL error. One CS assertion is exactly one frame, so the CS line *is* the
// framing and the receiver never has to hunt for a packet boundary.
//
// Peripheral: SPI1 in master mode, mode 0, MSB first, 1 MHz
//   PA4 CS (plain GPIO output, driven here)   PA5 SCK
//   PA6 MISO (unused — the slave never answers) PA7 MOSI
// Wired to the ESP32-C3 as PA4->GPIO7 (CS), PA5->GPIO4 (SCLK), PA6->GPIO5
// (MISO), PA7->GPIO6 (MOSI), common ground.
//
// SPI1 itself is configured by CubeMX (MX_SPI1_Init) and the CS pin by
// MX_GPIO_Init, which parks it high before anything else runs. main.c only
// calls TelemetryLink_Init() once and TelemetryLink_Poll() from the loop.

#include "stm32f4xx_hal.h"
#include <stdint.h>

// Latch the CS pin high and reset the counters. Call once after MX_SPI1_Init().
void TelemetryLink_Init(void);

// Non-blocking check of the send timer: once per TELEMETRY_PERIOD_MS build a
// frame from the latest sensor values and clock it out. Call every main-loop
// iteration.
//
// The transmit itself blocks, but only for the ~256 us that 32 bytes take at
// 1 MHz — two orders of magnitude under the OLED flush that shares this loop.
void TelemetryLink_Poll(void);

// Link counters for the display. framesSent/framesFailed are since boot; a
// rising framesFailed means the SPI peripheral itself refused or timed out.
//
// Note what this can NOT tell you: the link is one-way, so a frame the master
// clocked out successfully is counted as sent even if the slave was not armed
// to receive it. "Did it arrive?" is only answerable on the ESP32's console,
// where the sequence numbers show the gaps.
typedef struct {
    uint32_t framesSent;    // frames the SPI peripheral accepted and clocked out
    uint32_t framesFailed;  // HAL errors / timeouts on the transmit
    uint32_t lastSeq;       // sequence number of the most recent frame
} TelemetryLinkStats;

// Copy the latest counters into *out (no-op if out is NULL).
void TelemetryLink_GetStats(TelemetryLinkStats *out);

#endif // TELEMETRY_LINK_H
