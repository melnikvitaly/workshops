#pragma once
#include <driver/gpio.h>
#include <cstdint>

// One data-log record collected from a slave, handed to every sink.
struct LogRecord
{
    gpio_num_t     cs;           // CS pin the record came from
    uint32_t       seq;          // slave-assigned record id (monotonic per
                                 // device; dedup on it, e.g. an SD sink)
    uint32_t       eventTimeMs;  // when the slave logged it, on the master
                                 // timebase — or slave uptime if !timeSynced
    bool           timeSynced;   // true: eventTimeMs is master-referenced;
                                 // false: it is raw slave uptime
    int64_t        collectedAtUs;// master esp_timer time when it was polled
    const uint8_t* data;         // payload bytes (valid only during write())
    uint8_t        len;          // payload length
};

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
