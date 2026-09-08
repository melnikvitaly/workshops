#pragma once
#include <driver/gpio.h>

// Simple relay (or any on/off load) on a single GPIO.
// The laser module is switched through this. Many small relay boards are
// active-low, so the polarity is configurable.
class Relay
{
    gpio_num_t _pin;
    bool       _activeHigh;
    bool       _on = false;

public:
    explicit Relay(gpio_num_t pin, bool activeHigh = true)
        : _pin(pin), _activeHigh(activeHigh) {}

    void init()
    {
        // Boot-safe order (docs/interfaces.md §7): drive the inactive level
        // BEFORE gpio_config() makes the pin an output, and enable the internal
        // pull-up so the pre-init window rests off too. gpio_config() enables
        // the output first, which would otherwise briefly drive the reset-state
        // level - on the laser gate that lights the beam.
        const int inactiveLevel = _activeHigh ? 0 : 1;
        gpio_set_level(_pin, inactiveLevel);

        gpio_config_t io = {
            .pin_bit_mask = (1ULL << _pin),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = _activeHigh ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
            .pull_down_en = _activeHigh ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        off();
    }

    void set(bool on)
    {
        _on = on;
        gpio_set_level(_pin, (on == _activeHigh) ? 1 : 0);
    }

    void on()  { set(true); }
    void off() { set(false); }

    bool isOn() const { return _on; }
};
