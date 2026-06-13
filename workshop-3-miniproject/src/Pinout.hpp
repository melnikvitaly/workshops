#pragma once
#include <driver/gpio.h>
#include <hal/adc_types.h>

// ---------------------------------------------------------------------------
// Board wiring  --  TODO: set these to your actual wiring.
// (Defaults are placeholder ESP32-S3-DevKitC-1 choices.)
// ---------------------------------------------------------------------------
namespace pinout
{
    constexpr gpio_num_t SERVO_PAN  = GPIO_NUM_35;   // servo A (below) - horizontal
    constexpr gpio_num_t SERVO_TILT = GPIO_NUM_36;   // servo B (above) - vertical

    constexpr gpio_num_t ENC_A      = GPIO_NUM_18;   // encoder phase A (S1)
    constexpr gpio_num_t ENC_B      = GPIO_NUM_8;    // encoder phase B (S2)
    constexpr gpio_num_t ENC_SW     = GPIO_NUM_17;   // encoder push button (click)

    // On-board user button. NOTE: the RST button resets the MCU and cannot be
    // read as a GPIO; the usable on-board button is BOOT on GPIO0 (active-low,
    // board has its own pull-up). Click it to cycle the active input source.
    constexpr gpio_num_t BOOT_BTN   = GPIO_NUM_0;

    constexpr gpio_num_t RELAY      = GPIO_NUM_37;   // relay controlling the laser

    // Buzzer (passive) -- TODO: set to actual wiring. GPIO39 is free here (a
    // JTAG pin, unused since debugging runs over USB) and collides with nothing.
    constexpr gpio_num_t BUZZER     = GPIO_NUM_39;

    // Status indicator = onboard WS2812 RGB LED (GPIO48 on the DevKitC-1).
    constexpr gpio_num_t STATUS_LED = GPIO_NUM_48;

    // Potentiometer wiper: ADC1 channel 9 (GPIO10 on the S3).
    constexpr adc_unit_t    POT_ADC_UNIT = ADC_UNIT_1;
    constexpr adc_channel_t POT_ADC_CHAN = ADC_CHANNEL_9;
}
