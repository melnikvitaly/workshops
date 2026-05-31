#pragma once
#include <climits>
#include <esp_log.h>

class Ranges
{
    static constexpr const char *TAG = "RANGE";

    int _min = INT_MAX;
    int _max = INT_MIN;
    int _darkThreshold;
    int _lightThreshold;

    void logAdjustment() const
    {
        if (_max <= _min) return;
        ESP_LOGI(TAG, "raw[%4d..%4d] | dark=%4d light=%4d",
                 _min, _max, _darkThreshold, _lightThreshold);
    }

public:
    Ranges(int darkThreshold, int lightThreshold)
        : _darkThreshold(darkThreshold), _lightThreshold(lightThreshold) {}

    bool update(int raw)
    {
        bool changed = (raw < _min) || (raw > _max);
        if (raw < _min) _min = raw;
        if (raw > _max) _max = raw;
        if (changed) logAdjustment();
        return changed;
    }

    int darkThreshold()  const { return _darkThreshold; }
    int lightThreshold() const { return _lightThreshold; }
    int min() const { return _min == INT_MAX ? 0 : _min; }
    int max() const { return _max == INT_MIN ? 0 : _max; }
};
