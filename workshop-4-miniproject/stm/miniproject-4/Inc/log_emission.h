#ifndef LOG_EMISSION_H
#define LOG_EMISSION_H

// SPI-slave "data-log emission" module.
//
// Implements the slave side of the one-way streaming protocol used by the
// ESP32-C3 master (see espc3/src/LogProtocol.hpp — keep the two in sync). This
// board sits on the master's SPI bus as one of several devices selected by CS.
// It continuously presents self-framed log packets on MISO via free-running
// circular DMA; the master clocks blocks of that stream and parses records out
// of it. There is no request, no acknowledgement and no time sync — the master
// only ever clocks dummy bytes.
//
// Peripheral: SPI1 in hardware-NSS slave mode on PA4..PA7
//   PA4 NSS   PA5 SCK   PA6 MISO   PA7 MOSI   (AF5), mode 0, MSB first.
// SPI1_TX is driven by DMA2 Stream3 (circular). Everything (SPI/DMA init and the
// protocol) lives in this module; main.c only calls LogEmission_Init() once and
// LogEmission_Add*() to queue records.

#include "stm32f4xx_hal.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// SPI pin-debug mode. Set to 1 to replace the normal protocol stream with fixed,
// easy-to-scope test data so the physical SPI pins can be verified with a logic
// analyzer / debugger:
//   * MISO (slave -> master): the TX ring is filled with a repeating 0x00..0xFF
//     ramp instead of protocol packets — an unmistakable incrementing sawtooth
//     that also confirms MSB-first bit order and that MISO/SCK are alive.
//   * MOSI (master -> slave): SPI1_RX is captured by a second circular DMA; the
//     most recent master byte and a running count are exposed via
//     LogEmission_DebugRx() for watching in the debugger.
// With this ON the master will NOT see valid packets (it just clocks the ramp);
// it is a hardware bring-up aid. Set back to 0 for normal operation.
#define LOGEMIT_DEBUG_SPI 0

// Value types carried by a DataEntry. Numeric types are stored little-endian;
// LOGVT_STR is raw chars; LOGVT_DATETIME is a 6-byte LogDateTime. Mirror of
// logproto::ValueType on the master.
typedef enum {
    LOGVT_U8, LOGVT_U16, LOGVT_U32,
    LOGVT_I8, LOGVT_I16, LOGVT_I32,
    LOGVT_F32,
    LOGVT_STR,
    LOGVT_DATETIME,
} LogValueType;

// Object id length (fixed 4 chars, not NUL-terminated on the wire).
#define LOGEMIT_OBJID_LEN 4

// A broken-down date/time value (e.g. an RTC reading logged as a sensor). Binary
// (not BCD); `year` is 2000-based. Maps 1:1 onto the DS1307 driver's RtcTime
// fields (see Inc/ds1307.h) and onto logproto::DateTime on the master.
typedef struct {
    uint8_t year;    // 0..99 (2000 + year)
    uint8_t month;   // 1..12
    uint8_t date;    // 1..31
    uint8_t hours;   // 0..23
    uint8_t minutes; // 0..59
    uint8_t seconds; // 0..59
} LogDateTime;

// Bring up SPI1 as a slave, fill the stream with idle packets and start the
// circular DMA. Call once after the HAL/clock are initialised.
void LogEmission_Init(void);

// Queue a DataEntry: source object id (4 chars), value type, and up to 255
// payload bytes. Assigns the next monotonic record id and stamps it with the
// current uptime. Call from the main loop. If the stream ring is full (the
// master is absent or too slow) the oldest bytes are overwritten and the loss is
// counted and reported to the master.
void LogEmission_AddEntry(const char objectId[LOGEMIT_OBJID_LEN],
                          uint8_t valueType,
                          const void *payload, uint8_t payloadLen);

// Convenience wrappers.
void LogEmission_AddU32(const char objectId[LOGEMIT_OBJID_LEN], uint32_t value);
void LogEmission_AddText(const char objectId[LOGEMIT_OBJID_LEN], const char *text);
void LogEmission_AddDateTime(const char objectId[LOGEMIT_OBJID_LEN],
                             const LogDateTime *dt);

// Throughput snapshot for diagnostics/display. The two rates are recomputed once
// a second by LogEmission_ActivityPoll() over the actual elapsed time, so they
// stay honest even when the main loop overshoots (e.g. a blocking OLED flush).
typedef struct {
    uint32_t packetsPerSec;  // DataEntries queued in the last full window
    uint32_t bytesPerSec;    // bytes the TX DMA handed to SPI1 in that window
    uint32_t totalPackets;   // DataEntries queued since boot
    // Bytes clocked out since boot, counted in whole ring laps — the partial lap
    // in progress is not included, so this trails the truth by up to one ring and
    // the rate above is quantised to that step. Wraps at 2^32 (~9 h at 1 MHz).
    uint32_t totalBytes;
    uint16_t dropped;        // ring-overflow events reported to the master
} LogEmitStats;

// Copy the latest throughput snapshot into *out (no-op if out is NULL).
void LogEmission_GetStats(LogEmitStats *out);

// Drive the SPI-activity LED (on-board PC13). Detects bytes moving over SPI by
// watching the free-running TX DMA advance (each clocked byte is both received on
// MOSI and sent on MISO), lighting the LED on traffic and releasing it a short
// time after it stops. Cheap (GPIO writes + HAL_GetTick); call it from the main
// loop. Safe to call from an ISR too — it neither blocks nor delays.
void LogEmission_ActivityPoll(void);

#if LOGEMIT_DEBUG_SPI
// Debug: report SPI RX (MOSI) activity captured from the master. Writes the most
// recent byte the master clocked in to *lastByte (if non-NULL) and returns a
// running count of captured bytes. Watch these in a debugger to confirm MOSI is
// live end-to-end (master request bytes actually reaching the slave).
uint32_t LogEmission_DebugRx(uint8_t *lastByte);
#endif

#endif // LOG_EMISSION_H
