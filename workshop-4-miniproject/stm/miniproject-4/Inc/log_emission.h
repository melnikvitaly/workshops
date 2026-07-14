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

#endif // LOG_EMISSION_H
