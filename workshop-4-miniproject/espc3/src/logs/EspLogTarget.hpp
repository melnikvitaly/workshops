#pragma once
#include "LogsTarget.hpp"
#include <esp_log.h>
#include <cstdio>

// LogsTarget that prints each record to the ESP-IDF console log (esp_log), so
// records show up on the same UART0 monitor as the collector's own diagnostics
// — handy during bring-up with no extra wiring. Emitted at INFO level under the
// "REC" tag; raise/lower it with esp_log_level_set("REC", ...).
class EspLogTarget : public LogsTarget
{
public:
    explicit EspLogTarget(esp_log_level_t level = ESP_LOG_INFO) : _level(level) {}

    bool init() override { return true; } // nothing to bring up

    void write(const LogRecord& rec) override
    {
        char val[48];
        formatLogValue(val, sizeof(val), rec);
        ESP_LOG_LEVEL(_level, TAG, "CS%d #%u up=%u %.4s=%s",
                      (int)rec.cs, (unsigned)rec.seq, (unsigned)rec.eventTimeMs,
                      rec.objectId, val);
    }

    const char* name() const override { return "esplog"; }

private:
    static constexpr const char* TAG = "REC";
    esp_log_level_t _level;
};
