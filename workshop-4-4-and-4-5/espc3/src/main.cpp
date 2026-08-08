#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_attr.h>
#include <driver/gpio.h>
#include <cstdio>

#include "hardware/SpiSlave.hpp"
#include "hardware/StatusLed.hpp"
#include "TelemetryPrinter.hpp"
#include "telemetry_packet.h"   // shared wire format, compiled by the STM32 too

// ---------------------------------------------------------------------------
// Roles are the other way round from workshop-4-miniproject: there this board
// was the SPI master polling STM32 log-slaves, here it is the **slave** and the
// STM32 drives the clock. The wiring is unchanged — MOSI still meets MOSI and
// MISO still meets MISO, because those names are defined by the master's role,
// not by the pin's owner — but the data now flows STM32 -> ESP32 on MOSI, and
// MISO carries nothing at all.
// ---------------------------------------------------------------------------

// SPI2 (FSPI) — SPI0/1 are reserved for the flash on the C3.
static constexpr spi_host_device_t HOST = SPI2_HOST;

// Same four wires as the mini-project, same pins:
//   ESP GPIO6 (MOSI) <- STM PA7 (MOSI)   the telemetry frame
//   ESP GPIO5 (MISO) -> STM PA6 (MISO)   unused, one-way link
//   ESP GPIO4 (SCLK) <- STM PA5 (SCK)    master's clock
//   ESP GPIO7 (CS)   <- STM PA4 (GPIO)   one assertion = one frame
// Pin choices are constrained by the C3: GPIO11-17 are the SPI flash, 18/19 are
// USB, 20/21 are the UART0 console. Validate against those before changing any.
static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_6;
static constexpr gpio_num_t PIN_MISO = GPIO_NUM_5;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_4;
static constexpr gpio_num_t PIN_CS   = GPIO_NUM_7;

// Mode 0 (CPOL=0, CPHA=0), MSB first — must match MX_SPI1_Init on the STM32.
static constexpr uint8_t SPI_MODE = 0;

// Activity LED. On the plain-LED C3 boards GPIO8 is the single user LED, wired
// active-low. (On a DevKitM-1 the on-board LED is an addressable WS2812 and a
// level pulse does nothing visible — harmless either way.)
static constexpr gpio_num_t PIN_LED = GPIO_NUM_8;

// How long to wait for a frame before saying so. The master sends once a
// second, so a few seconds of silence is a real problem (wiring, or the master
// not running) and worth printing rather than hanging mutely forever.
static constexpr uint32_t QUIET_NOTICE_MS = 5000;

static SpiSlave         slave(HOST, PIN_MOSI, PIN_MISO, PIN_SCLK, PIN_CS, SPI_MODE);
static StatusLed        led(PIN_LED, /*pulseMs=*/25, /*activeHigh=*/false);
static TelemetryPrinter printer;

// The receive buffer. WORD_ALIGNED_ATTR because the SPI peripheral moves whole
// words in and out of its FIFO; static because the driver writes into it while
// receive() is not on the stack.
static WORD_ALIGNED_ATTR uint8_t g_rx[TELEMETRY_FRAME_SIZE];

static const char* TAG = "TELEMETRY_SLAVE";

static void printBanner()
{
    std::printf("\n");
    std::printf("=====================================================\n");
    std::printf(" ESP32-C3 telemetry receiver  (SPI SLAVE)\n");
    std::printf("=====================================================\n");
    std::printf(" master : STM32F401 (SPI1, mode %u, 1 MHz)\n", (unsigned)SPI_MODE);
    std::printf(" wiring : MOSI=GPIO%d  MISO=GPIO%d  SCLK=GPIO%d  CS=GPIO%d\n",
                (int)PIN_MOSI, (int)PIN_MISO, (int)PIN_SCLK, (int)PIN_CS);
    std::printf(" frame  : %d bytes, one per CS assertion, ~1 per second\n",
                TELEMETRY_FRAME_SIZE);
    std::printf(" carries: date, time, temperature, light %%, humidity, pressure\n");
    std::printf("=====================================================\n");
    std::printf("waiting for the first frame...\n");
}

extern "C" void app_main()
{
    // Give the native USB CDC (USB-Serial/JTAG) time to re-enumerate after the
    // reset that just brought us here, and you time to reattach `pio device
    // monitor` — without this the banner below is gone before you can connect.
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_ERROR_CHECK(slave.init());
    ESP_ERROR_CHECK(led.init());

    printBanner();

    const TickType_t quietTicks = pdMS_TO_TICKS(QUIET_NOTICE_MS);

    while (true)
    {
        size_t    received = 0;
        esp_err_t r        = slave.receive(g_rx, sizeof(g_rx), quietTicks, received);

        if (r == ESP_ERR_TIMEOUT)
        {
            // Nothing arrived within the window. The transaction is still armed
            // (see SpiSlave::receive), so this costs nothing but the notice.
            ESP_LOGW(TAG, "no frame for %u ms — is the STM32 master running and CS/SCLK wired?",
                     (unsigned)QUIET_NOTICE_MS);
            continue;
        }
        if (r != ESP_OK)
        {
            ESP_LOGE(TAG, "spi slave receive failed: %s", esp_err_to_name(r));
            continue;
        }

        led.pulse();

        if (received != TELEMETRY_FRAME_SIZE)
        {
            // The master clocked a different number of bytes than one frame.
            // Almost always a clock-rate or CS-timing problem rather than data
            // corruption, so it is worth distinguishing from a CRC failure.
            printer.countBad();
            ESP_LOGW(TAG, "short frame: %u bytes, expected %d",
                     (unsigned)received, TELEMETRY_FRAME_SIZE);
            continue;
        }

        TelemetryPayload payload;
        TelemetryStatus  status = Telemetry_Parse(g_rx, &payload);
        if (status != TELEMETRY_OK)
        {
            printer.countBad();
            ESP_LOGW(TAG, "frame rejected: %s", Telemetry_StatusName(status));
            continue;
        }

        printer.print(payload);
    }
}
