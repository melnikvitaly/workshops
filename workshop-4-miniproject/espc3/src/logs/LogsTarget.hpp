#pragma once
#include "../LogProtocol.hpp"
#include <driver/gpio.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// One data-log record (a DataEntry) collected from a slave, handed to every
// sink. objectId/valueType/data describe the typed value the slave logged.
struct LogRecord
{
    gpio_num_t     cs;           // CS pin the record came from
    uint32_t       seq;          // slave-assigned record id (monotonic per
                                 // device; dedup on it, e.g. an SD sink)
    uint32_t       eventTimeMs;  // slave uptime (ms) when the record was queued
    int64_t        collectedAtUs;// master esp_timer time when it was polled
    char           objectId[4];  // source object id (4 chars, not NUL-term)
    uint8_t        valueType;    // logproto::ValueType of the payload
    const uint8_t* data;         // payload bytes (valid only during write())
    uint8_t        len;          // payload length (bytes)
};

// Render a record's typed value as text into `out` (snprintf-style, always
// NUL-terminated). Shared by text sinks so the per-type formatting lives in one
// place. Numeric types are little-endian; VT_DATETIME is "20YY-MM-DD HH:MM:SS".
inline int formatLogValue(char* out, size_t cap, const LogRecord& rec)
{
    const uint8_t* d = rec.data;
    auto le = [&](int nbytes) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < nbytes && i < rec.len; ++i)
            v |= (uint32_t)d[i] << (8 * i);
        return v;
    };
    switch (rec.valueType)
    {
    case logproto::VT_U8:  return snprintf(out, cap, "%u", (unsigned)le(1));
    case logproto::VT_U16: return snprintf(out, cap, "%u", (unsigned)le(2));
    case logproto::VT_U32: return snprintf(out, cap, "%u", (unsigned)le(4));
    case logproto::VT_I8:  return snprintf(out, cap, "%d", (int)(int8_t)le(1));
    case logproto::VT_I16: return snprintf(out, cap, "%d", (int)(int16_t)le(2));
    case logproto::VT_I32: return snprintf(out, cap, "%d", (int)(int32_t)le(4));
    case logproto::VT_F32:
    {
        uint32_t bits = le(4); float f; memcpy(&f, &bits, sizeof(f));
        return snprintf(out, cap, "%.3f", f);
    }
    case logproto::VT_DATETIME:
        if (rec.len >= 6)
            return snprintf(out, cap, "20%02u-%02u-%02u %02u:%02u:%02u",
                            d[0], d[1], d[2], d[3], d[4], d[5]);
        return snprintf(out, cap, "(bad datetime)");
    case logproto::VT_STR:
    default:
        return snprintf(out, cap, "%.*s", (int)rec.len, (const char*)d);
    }
}

// Abstract sink for collected records. Implementations persist or emit a record
// however they like (UART now, SD card later). The collector fans every record
// out to all registered targets, so multiple sinks run at the same time.
//
// write() is called synchronously from the collector's scan task while the
// record's payload buffer is still valid; a target that needs the bytes beyond
// the call must copy them.
class LogsTarget
{
public:
    virtual ~LogsTarget() = default;

    // Bring the sink up. Return false if it cannot be used (collector then
    // skips it). Must tolerate being called once before scanning starts.
    virtual bool init() = 0;

    // Emit/persist one record.
    virtual void write(const LogRecord& rec) = 0;

    // Short identifier for diagnostics/logging.
    virtual const char* name() const = 0;
};
