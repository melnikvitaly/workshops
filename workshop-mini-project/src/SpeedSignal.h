#pragma once
#include "hardware/BoardLED.h"
#include "hardware/Debug.h"
#include "RPMCounter.h"

/// @brief Checks if some RPM Sensors have detected speed higher then threshold
/// if detected  - light LED in RED, otherwise to white
class SpeedSignal
{
    BoardLED &led;
    Debug &dbg;
    const static uint32_t maxAllowedRPM = 80;
    RPMCounter *targets[10];
    uint8_t targetCount = 0;

public:
    SpeedSignal(BoardLED &led, Debug &dbg)
        : led(led), dbg(dbg)
    {
    }

    void track(RPMCounter *target)
    {
        targets[targetCount] = target;
        targetCount++;
    }

    void tick()
    {

        for (size_t i = 0; i < targetCount; i++)
        {
            if (targets[i]->getRPM() > maxAllowedRPM)
            {
                led.red();
                return;
            }
        }

        led.white();
    }
};