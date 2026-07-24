#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "hardware/SpiBus.hpp"
#include "hardware/I2cBus.hpp"
#include "hardware/StatusLed.hpp"
#include "logs/UartTarget.hpp"
#include "logs/OledTarget.hpp"
#include "logs/EspLogTarget.hpp"
#include "DataLogCollector.hpp"

// SPI2 (FSPI) — SPI0/1 are reserved for flash (per the lecture).
static constexpr spi_host_device_t HOST = SPI2_HOST;

// Shared bus lines. Use the requested ESP SPI pins for the bus.
static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_6;
static constexpr gpio_num_t PIN_MISO = GPIO_NUM_5;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_4;

// Predefined CS pins to scan. Plug a log-slave onto any of these and it is
// picked up automatically on the next pass; unplug it and it is dropped.
static constexpr gpio_num_t CS_PINS[] = {
    GPIO_NUM_7,
};
static constexpr int      CS_COUNT       = sizeof(CS_PINS) / sizeof(CS_PINS[0]);
static constexpr uint32_t SCAN_PERIOD_MS = 1000;
static constexpr uint32_t STATUS_PERIOD_MS = 10000;

// SPI activity LED — the board's on-board LED on GPIO8. On the ESP32-C3-DevKitM-1
// (and the pin-compatible C3 mini boards), GPIO8 is the single user LED; see the
// board user guide linked in the PlatformIO board JSON and the ESP-IDF `blink`
// example, which both target GPIO8 for this board. It blinks briefly on every SPI
// block transfer. Active-low to match the common C3 on-board LED wiring (drive
// the pin low to light it); flip the activeHigh arg below if your board differs.
// NOTE: the DevKitM-1's on-board LED is an addressable WS2812 (RGB) rather than a
// plain level-driven LED — a simple GPIO pulse suits the plain-LED C3 boards; a
// WS2812 needs the led_strip driver to render colour.
static constexpr gpio_num_t PIN_SPI_LED = GPIO_NUM_8;

// I2C for the OLED sink. Rides GPIO10 (SDA) / GPIO9 (SCL): GPIO10 is free once
// SPI (4/5/6), CS (7), UART1 (0) and the on-board LED (8) are assigned, and is
// not a strapping/flash/USB/UART pin. SDA moved off GPIO8 so the on-board LED can
// own it. I2C idles high after boot; external pull-ups recommended.
static constexpr gpio_num_t PIN_SDA = GPIO_NUM_10;
static constexpr gpio_num_t PIN_SCL = GPIO_NUM_9;

static SpiBus           bus(HOST, PIN_MOSI, PIN_MISO, PIN_SCLK);
static I2cBus           i2cBus(I2C_NUM_0, PIN_SDA, PIN_SCL);
// SPI clock. Bring-up used a low rate for signal-integrity margin; with the SCK
// series resistor damping ringing, 1 MHz is safe and gives ample headroom
// (~4 ms per 512-byte block). Step down toward 250-500 kHz if bit-slip or drops
// reappear on the link.
static DataLogCollector collector(bus, CS_PINS, CS_COUNT, 1'000'000);

// Brief non-blocking activity blink each time the master clocks a block of bytes.
// activeHigh=false: the C3 on-board LED lights when the pin is driven low.
static StatusLed        spiLed(PIN_SPI_LED, /*pulseMs=*/15, /*activeHigh=*/false);

// Log sinks. Add more here (e.g. an SdCardTarget) — the collector fans every
// record out to all of them, so they run side by side.
static UartTarget   uartTarget(UART_NUM_1, GPIO_NUM_0, 115200);
static OledTarget   oledTarget(i2cBus, 0x3C);
static EspLogTarget espLogTarget;   // prints records to the ESP-IDF console log

static const char* TAG = "LOG_MASTER";

extern "C" void app_main()
{
    ESP_ERROR_CHECK(bus.init());
    ESP_ERROR_CHECK(i2cBus.init());
    ESP_ERROR_CHECK(spiLed.init());
    collector.setStatusLed(&spiLed);

    collector.addTarget(&uartTarget);
    collector.addTarget(&oledTarget);
    collector.addTarget(&espLogTarget);

    ESP_LOGI(TAG, "SPI data-log collector up. Draining %d CS pins continuously, "
             "probing for hot-plug every %u ms.", CS_COUNT, (unsigned)SCAN_PERIOD_MS);

    // Two independent cadences: drainPresent() runs flat-out (no delay) so data
    // is collected continuously; probeAbsent() runs once per SCAN_PERIOD_MS just
    // to notice newly plugged devices. lastProbe is back-dated one period so the
    // first pass probes immediately (otherwise nothing is found for one period).
    const TickType_t probePeriod  = pdMS_TO_TICKS(SCAN_PERIOD_MS);
    const TickType_t statusPeriod = pdMS_TO_TICKS(STATUS_PERIOD_MS);
    TickType_t lastProbeTicks  = xTaskGetTickCount() - probePeriod;
    TickType_t lastStatusTicks = xTaskGetTickCount();

    while (true)
    {
        // Continuous, no-delay draining of every present device. While at least
        // one device is present this blocks inside the SPI transfers, which is
        // what yields the CPU to other tasks (and the watchdog).
        int present = collector.drainPresent();

        TickType_t now = xTaskGetTickCount();

        // Periodic hot-plug probe of the absent pins only.
        if ((now - lastProbeTicks) >= probePeriod)
        {
            present = collector.probeAbsent();
            lastProbeTicks = now;
        }

        if ((now - lastStatusTicks) >= statusPeriod)
        {
            collector.logStatus();
            lastStatusTicks = now;
        }

        // Nothing to drain: sleep until the next probe is due instead of spinning
        // a tight empty loop. With a device present we never get here — the drain
        // transfers pace the loop and keep collection continuous.
        if (present == 0)
        {
            TickType_t elapsed = xTaskGetTickCount() - lastProbeTicks;
            if (elapsed < probePeriod)
                vTaskDelay(probePeriod - elapsed);
        }
    }
}
