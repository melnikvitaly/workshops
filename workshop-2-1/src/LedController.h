#pragma once
#include <Arduino.h>
#include <hardware/Led.h>
#include <Config.h>

class LedController
{
    LED &led;

public:
    LedController(LED &led) : led(led)
    {
    }

    void start()
    {
        led.blink(Config::BLINK_TIME_MS);
    }

    void tick()
    {
    }

    void onChangeModePressed()
    {
        switch (led.state())
        {
        case LED::LEDState::BLINKING:
            led.set(LED::LEDState::ON);
            break;
        case LED::LEDState::ON:
            led.set(LED::LEDState::OFF);
            break;
        case LED::LEDState::OFF:
            led.set(LED::LEDState::BLINKING, Config::BLINK_TIME_MS);
            break;
        }
    }
};