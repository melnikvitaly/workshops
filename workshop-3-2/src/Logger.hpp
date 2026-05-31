#pragma once
#include <esp_log.h>

class Logger
{
    static constexpr int CHANGE_THRESHOLD = 5;

    const char *_tag;
    int _lastRaw  = -1;
    int _lastFilt = -1;

    static bool changed(int prev, int curr)
    {
        int diff = curr - prev;
        return diff > CHANGE_THRESHOLD || diff < -CHANGE_THRESHOLD;
    }

public:
    explicit Logger(const char *tag) : _tag(tag) {}

    void begin()
    {
        ESP_LOGI(_tag, "\n%-5s | %-5s | %-23s | %s",
                 "ADC", "SMA", "RANGE [min|drk|lgt|max]", "LED");
        ESP_LOGI(_tag, "-------|-------|-------------------------|----");
    }

    void log(int adcRaw, int smaRaw,
             int rangeMin, int rawDark, int rawLight, int rangeMax,
             const char *ledState)
    {
        if (!changed(_lastRaw, adcRaw) && !changed(_lastFilt, smaRaw)) return;
        _lastRaw  = adcRaw;
        _lastFilt = smaRaw;
        ESP_LOGI(_tag, "%5d | %5d | [%4d|%4d|%4d|%4d] | %s",
                 adcRaw, smaRaw, rangeMin, rawDark, rawLight, rangeMax, ledState);
    }
};
