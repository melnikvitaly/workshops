#pragma once
#include <hardware/Debug.h>
#include <Config.h>
#include <esp_timer.h>
#include <string>

class LoopTracker
{
    uint32_t i = 0;
    bool track = false;
    uint32_t startedAt = 0;
    Debug &dbg;

    static uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

public:
    LoopTracker(Debug &dbg) : dbg(dbg)
    {
    }

    void loopStart()
    {
        if (i == 0)
            startedAt = millis();
        i++;
        track = i % Config::LOOP_TRACK_ITERATIONS == 0;
    }

    void loopEnd()
    {
        if (track)
        {
            uint32_t now = millis();
            auto elapsed = now - startedAt;
            dbg.print(std::to_string(elapsed) + "ms / " +
                      std::to_string(Config::LOOP_TRACK_ITERATIONS) + " iters");
            startedAt = now;
        }
    }
};
