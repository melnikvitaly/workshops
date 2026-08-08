#pragma once
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <cstdint>

// A simple GPIO-driven activity LED. pulse() lights the LED and arms a one-shot
// esp_timer that switches it back off a few milliseconds later, so the caller
// never busy-waits — important on the SPI data path, where a blocking blink
// would eat into the throughput of the block transfer it is meant to indicate.
// Re-triggering while a pulse is still pending just restarts the off-timer (the
// LED stays lit a touch longer) instead of stacking timers.
//
// Header-only, following the SpiBus/SpiDevice style: the constructor stores the
// config and init() performs the ESP-IDF calls.
class StatusLed
{
    gpio_num_t         _pin;
    uint32_t           _pulseMs;
    bool               _activeHigh;
    esp_timer_handle_t _offTimer = nullptr;

    void on()  { gpio_set_level(_pin, _activeHigh ? 1 : 0); }
    void off() { gpio_set_level(_pin, _activeHigh ? 0 : 1); }

    static void offCb(void* arg) { static_cast<StatusLed*>(arg)->off(); }

public:
    explicit StatusLed(gpio_num_t pin,
                       uint32_t   pulseMs    = 15,
                       bool       activeHigh = true)
        : _pin(pin), _pulseMs(pulseMs), _activeHigh(activeHigh) {}

    esp_err_t init()
    {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << _pin;
        io.mode         = GPIO_MODE_OUTPUT;
        io.pull_up_en   = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type    = GPIO_INTR_DISABLE;
        esp_err_t r = gpio_config(&io);
        if (r != ESP_OK)
            return r;
        off();

        esp_timer_create_args_t targs = {};
        targs.callback = &offCb;
        targs.arg      = this;
        targs.name     = "led_off";
        return esp_timer_create(&targs, &_offTimer);
    }

    // Non-blocking: light the LED now and schedule it off in _pulseMs. Fire this
    // each time bytes are clocked over SPI for a brief activity blink.
    void pulse()
    {
        on();
        if (_offTimer)
        {
            esp_timer_stop(_offTimer);                     // restart if pending
            esp_timer_start_once(_offTimer, (uint64_t)_pulseMs * 1000);
        }
    }
};
