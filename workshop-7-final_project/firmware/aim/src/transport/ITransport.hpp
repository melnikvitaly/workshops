#pragma once
#include <cstddef>

// The link seam. Phase 0 has one implementation, UartTransport on UART1;
// MqttTransport and EspNowTransport drop in behind this in Phase 1
// (docs/architecture.md §3, TASKS.md task #3). Line-oriented: one frame per
// line, terminator handled by the implementation.
class ITransport
{
public:
    virtual ~ITransport() = default;

    virtual void init() = 0;

    // Return one complete received line (without the terminator) into `out`, or
    // false if none is available. May block up to a short internal timeout.
    virtual bool readLine(char *out, size_t cap) = 0;

    // Write one whole line; the terminator is appended. Safe to call from any
    // task - writes are serialised internally.
    virtual void writeLine(const char *line) = 0;
};
