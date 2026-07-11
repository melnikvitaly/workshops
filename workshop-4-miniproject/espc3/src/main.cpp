#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "hardware/SpiBus.hpp"
#include "logs/UartTarget.hpp"
#include "DataLogCollector.hpp"

// SPI2 (FSPI) — SPI0/1 are reserved for flash (per the lecture).
static constexpr spi_host_device_t HOST = SPI2_HOST;

// Shared bus lines. On the ESP32-C3, GPIO11-17 are the SPI flash, 18/19 are
// USB and 20/21 the UART0 console, so the bus rides on free low GPIOs
// (routed through the GPIO matrix to FSPI).
static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_7;
static constexpr gpio_num_t PIN_MISO = GPIO_NUM_1;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_6;

// Predefined CS pins to scan. Plug a log-slave onto any of these and it is
// picked up automatically on the next pass; unplug it and it is dropped.
// (Kept off the C3 strapping pins 2/8/9 and the reserved ranges above.)
static constexpr gpio_num_t CS_PINS[] = {
    GPIO_NUM_10,
    GPIO_NUM_3,
    GPIO_NUM_4,
    GPIO_NUM_5,
};
static constexpr int      CS_COUNT       = sizeof(CS_PINS) / sizeof(CS_PINS[0]);
static constexpr uint32_t SCAN_PERIOD_MS = 1000;

static SpiBus           bus(HOST, PIN_MOSI, PIN_MISO, PIN_SCLK);
static DataLogCollector collector(bus, CS_PINS, CS_COUNT);

// Log sinks. Add more here (e.g. an SdCardTarget) — the collector fans every
// record out to all of them, so they run side by side.
static UartTarget uartTarget(UART_NUM_1, GPIO_NUM_0, 115200);

static const char* TAG = "LOG_MASTER";

extern "C" void app_main()
{
    ESP_ERROR_CHECK(bus.init());

    collector.addTarget(&uartTarget);

    ESP_LOGI(TAG, "SPI data-log collector up. Scanning %d CS pins every %u ms.",
             CS_COUNT, (unsigned)SCAN_PERIOD_MS);

    while (true)
    {
        collector.scan();
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
