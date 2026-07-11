#ifndef LOG_EMISSION_H
#define LOG_EMISSION_H

// SPI-slave "data-log emission" module.
//
// Implements the slave side of the protocol defined for the ESP32-C3 master
// (see espc3/src/LogProtocol.hpp — keep the two in sync). This board sits on the
// master's SPI bus as one of several devices selected by CS; the master
// periodically polls it, pushes a reference clock, and drains buffered log
// records with a cumulative acknowledgement by record id.
//
// Peripheral: SPI1 in hardware-NSS slave mode on PA4..PA7
//   PA4 NSS   PA5 SCK   PA6 MISO   PA7 MOSI   (AF5), mode 0, MSB first.
// Everything (SPI init, MSP, IRQ, protocol) lives in this module; main.c only
// calls LogEmission_Init() once and LogEmission_Add*() to queue records.

#include "stm32f4xx_hal.h"
#include <stdint.h>

// Max payload bytes per record. Mirror of logproto::PAYLOAD_MAX on the master.
#define LOGEMIT_PAYLOAD_MAX 18

// Bring up SPI1 as a slave and arm the first transaction. Call once after the
// HAL/clock are initialised.
void LogEmission_Init(void);

// Queue a new log record (copies up to LOGEMIT_PAYLOAD_MAX bytes). Assigns the
// next monotonic record id and stamps it with the current clock. Call from the
// main loop; the SPI IRQ is briefly masked while the ring is updated. If the
// ring is full the oldest unacked record is dropped (leaving an id gap, which
// the master tolerates).
void LogEmission_Add(const uint8_t *data, uint8_t len);

// Convenience wrapper: queue a NUL-terminated string (terminator not sent).
void LogEmission_AddText(const char *text);

#endif // LOG_EMISSION_H
