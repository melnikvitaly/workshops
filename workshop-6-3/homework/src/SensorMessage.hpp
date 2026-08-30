#pragma once
#include <cstdint>

enum class SensorId : uint8_t
{
    Temperature,
    Light,
};

struct SensorMessage
{
    SensorId id;
    uint32_t timestampMs;
    union
    {
        float celsius;
        uint8_t percent;
    } value;
};

inline const char *sensorName(SensorId id)
{
    switch (id)
    {
    case SensorId::Temperature:
        return "temperature";
    case SensorId::Light:
        return "light";
    }
    return "unknown";
}
