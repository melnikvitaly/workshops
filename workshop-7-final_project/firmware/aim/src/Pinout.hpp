#pragma once
#include <cstdint>
#include <driver/gpio.h>
#include <driver/uart.h>

// Authoritative pin map: docs/interfaces.md §1. That table is the source of
// truth; this header is its implementation and the two must be kept in step.
//
// Module: ESP32-S3-WROOM-1, quad-flash / no octal PSRAM (N4 / N8). GPIO33-37
// are free only on non-octal parts - see docs/interfaces.md §1.2.
namespace pinout
{
    // --- Servos (LEDC) -----------------------------------------------------
    constexpr gpio_num_t SERVO_PAN  = GPIO_NUM_35; // servo A (below) - horizontal
    constexpr gpio_num_t SERVO_TILT = GPIO_NUM_36; // servo B (above) - vertical

    // --- Laser gate ------------------------------------------------------------
    // MOSFET gate. Internal pull-up + level-before-config so the pre-init window
    // rests off (docs/interfaces.md §7). External pull-up is the Phase 1 board fix.
    constexpr gpio_num_t LASER_GATE = GPIO_NUM_6;

    // --- micro-SD on SPI2 / FSPI (IOMUX pins) --------------------------------
    // No card-detect line: the socket has no CD switch, so "removed while
    // running" is caught on the next failed write (docs/interfaces.md §4.1).
    constexpr gpio_num_t SD_CS   = GPIO_NUM_10;
    constexpr gpio_num_t SD_MOSI = GPIO_NUM_11;
    constexpr gpio_num_t SD_SCK  = GPIO_NUM_12;
    constexpr gpio_num_t SD_MISO = GPIO_NUM_13;

    // --- OLED on I2C0 --------------------------------------------------------
    constexpr int        OLED_I2C_PORT = 0; // I2C_NUM_0
    constexpr gpio_num_t OLED_SDA      = GPIO_NUM_15;
    constexpr gpio_num_t OLED_SCL      = GPIO_NUM_16;
    constexpr uint8_t    OLED_ADDR     = 0x3C; // 0x3D if the module jumper is moved
    constexpr uint32_t   OLED_HZ       = 400000;

    // --- EYE link on UART1 --------------------------------------------------
    // UART0 stays console-only; no data crosses it. 17/18 are adjacent on the
    // module edge and neither is a strapping pin.
    constexpr uart_port_t LINK_UART      = UART_NUM_1;
    constexpr gpio_num_t  LINK_TX        = GPIO_NUM_17; // -> adapter RX
    constexpr gpio_num_t  LINK_RX        = GPIO_NUM_18; // <- adapter TX
    constexpr int         LINK_UART_BAUD = 115200;
    constexpr int         LINK_RX_BUF    = 1024;
    constexpr int         LINK_TX_BUF    = 512;
    constexpr int         LINK_EVT_QUEUE = 16;

    // --- Buttons ----------------------------------------------------------------
    // E-stop is the only button on an interrupt. MODE and CONTROL are polled by
    // the ui task. All are active-low with an RC on the board and an internal
    // pull-up in firmware.
    constexpr gpio_num_t BTN_ESTOP   = GPIO_NUM_4; // ISR, neg-edge
    constexpr gpio_num_t BTN_MODE    = GPIO_NUM_5; // polled 50 Hz - non-strapping
    constexpr gpio_num_t BTN_CONTROL = GPIO_NUM_7; // polled - arm / fault-ack

    // --- Misc -------------------------------------------------------------------
    constexpr gpio_num_t VBAT_SENSE = GPIO_NUM_2;  // ADC1_CH1, 2:1 divider - task #10
    constexpr gpio_num_t SCOPE      = GPIO_NUM_47; // toggled across the control step - task #5
    constexpr gpio_num_t STATUS_LED = GPIO_NUM_48; // onboard WS2812

    // Free and uncommitted: 1, 9, 14 (21/33/34/37/38 held for the Phase 1 SPI slave link).
}
