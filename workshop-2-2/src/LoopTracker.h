#pragma once
#include <Arduino.h>
#include <hardware/Debug.h>
#include <Config.h>

class LoopTracker
{
    uint32_t i = 0;
    bool track = false;
    uint32_t startedAt = 0;
    Debug &dbg;

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
            dbg.print(String(elapsed) + "ms / " + String(Config::LOOP_TRACK_ITERATIONS) + " iters");
            startedAt = now;
        }
    }
};