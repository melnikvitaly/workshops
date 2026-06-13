#pragma once
#include <led_strip.h>

// Onboard status indicator.
//
// The ESP32-S3-DevKitC-1 does not have a plain LED on a GPIO; it has a single
// addressable WS2812 ("NeoPixel") RGB LED, by default on GPIO48. It cannot be
// driven by a simple gpio_set_level() - it needs the timed serial protocol the
// led_strip driver generates over RMT. This thin wrapper hides that so the
// rest of the code can just ask for a colour.
class StatusLed
{
    static constexpr uint32_t LED_COUNT   = 1;                // single onboard pixel
    static constexpr uint32_t PIXEL_INDEX = 0;                // index of that pixel
    static constexpr uint32_t RMT_RES_HZ  = 10 * 1000 * 1000; // 10 MHz RMT tick

    gpio_num_t         _pin;
    led_strip_handle_t _strip = nullptr;

public:
    explicit StatusLed(gpio_num_t pin = GPIO_NUM_48) : _pin(pin) {}

    void init()
    {
        led_strip_config_t stripCfg = {
            .strip_gpio_num   = _pin,
            .max_leds         = LED_COUNT,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,
            .led_model        = LED_MODEL_WS2812,
            .flags            = { .invert_out = false },
        };
        led_strip_rmt_config_t rmtCfg = {
            .clk_src           = RMT_CLK_SRC_DEFAULT,
            .resolution_hz     = RMT_RES_HZ,
            .mem_block_symbols = 0,
            .flags             = { .with_dma = false },
        };
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&stripCfg, &rmtCfg, &_strip));
        off();
    }

    void rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        led_strip_set_pixel(_strip, PIXEL_INDEX, r, g, b);
        led_strip_refresh(_strip);
    }

    void off() { led_strip_clear(_strip); }
};
