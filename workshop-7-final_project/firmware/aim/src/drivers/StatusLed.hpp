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
        // led_strip 3.x renamed led_pixel_format -> color_component_format and
        // now describes the ordering with a helper macro instead of an enum.
        // Value-initialise then assign, so future members added by the
        // component do not break the build under -Werror.
        led_strip_config_t stripCfg = {};
        stripCfg.strip_gpio_num         = _pin;
        stripCfg.max_leds               = LED_COUNT;
        stripCfg.led_model              = LED_MODEL_WS2812;
        stripCfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
        stripCfg.flags.invert_out       = false;

        led_strip_rmt_config_t rmtCfg = {};
        rmtCfg.clk_src           = RMT_CLK_SRC_DEFAULT;
        rmtCfg.resolution_hz     = RMT_RES_HZ;
        rmtCfg.mem_block_symbols = 0;
        rmtCfg.flags.with_dma    = false;

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
