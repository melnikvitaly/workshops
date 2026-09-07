#pragma once
#include <driver/gpio.h>
#include <driver/uart.h>

namespace pinout
{
    constexpr gpio_num_t SERVO_PAN  = GPIO_NUM_35;   // servo A (below) - horizontal
    constexpr gpio_num_t SERVO_TILT = GPIO_NUM_36;   // servo B (above) - vertical

    // The encoder itself is gone in this project (the PC closes the loop), but
    // its push button is still wired and makes a convenient laser control.
    constexpr gpio_num_t CONTROL_BTN = GPIO_NUM_17;  // click: flash, hold: constant on
    constexpr gpio_num_t BOOT_BTN    = GPIO_NUM_0;   // arm / disarm the tracking loop

    constexpr gpio_num_t RELAY      = GPIO_NUM_6;    // relay controlling the laser

    // Buzzer (passive). GPIO39 is a JTAG pin, unused since debugging runs over
    // USB, and collides with nothing.
    constexpr gpio_num_t BUZZER     = GPIO_NUM_39;

    // Status indicator = onboard WS2812 RGB LED (GPIO48 on the DevKitC-1).
    constexpr gpio_num_t STATUS_LED = GPIO_NUM_48;

    // Error-vector stream from the PC. UART0 is the console port (GPIO43/44,
    // the CP2102 "UART" connector) - the same one PlatformIO's monitor uses, so
    // no extra wiring is needed. See Uart.hpp for how RX coexists with logging.
    constexpr uart_port_t LINK_UART      = UART_NUM_0;
    constexpr int         LINK_UART_BAUD = 115200;

    // Free after dropping the pot and encoder: GPIO8, GPIO10, GPIO18.
}
