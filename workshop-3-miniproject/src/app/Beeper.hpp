#pragma once
#include "Buzzer.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Non-blocking one-shot tone player.
//
// beep() starts a tone of a given frequency for a given duration and returns
// immediately; update(), polled from the main loop exactly like Laser::update,
// silences it once the time is up. No vTaskDelay, no melodies - just short
// feedback beeps. (All PWM/LEDC access happens here on the loop, never in an ISR.)
class Beeper
{
    Buzzer&  _buzzer;
    bool     _active  = false;
    uint32_t _untilMs = 0;

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

public:
    explicit Beeper(Buzzer& buzzer) : _buzzer(buzzer) {}

    void init() { _buzzer.init(); }

    // Play freqHz for durationMs, then auto-silence (via update()).
    void beep(uint32_t freqHz, uint32_t durationMs)
    {
        _buzzer.playNote(freqHz);
        _active  = true;
        _untilMs = nowMs() + durationMs;
    }

    // Call often; silences the buzzer once the current beep elapses.
    void update()
    {
        if (_active && (int32_t)(nowMs() - _untilMs) >= 0)
        {
            _buzzer.silence();
            _active = false;
        }
    }

    bool isBeeping() const { return _active; }
};
