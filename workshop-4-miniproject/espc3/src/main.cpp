#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "hardware/SpiBus.hpp"
#include "hardware/I2cBus.hpp"
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

// I2C for the OLED sink. The C3's free pins are scarce once SPI (7/1/6), the CS
// set (10/3/4/5) and UART1 (0) are taken, so I2C rides the strapping pins 8/9 —
// fine after boot since I2C idles high (external pull-ups recommended).
static constexpr gpio_num_t PIN_SDA = GPIO_NUM_8;
static constexpr gpio_num_t PIN_SCL = GPIO_NUM_9;

static SpiBus           bus(HOST, PIN_MOSI, PIN_MISO, PIN_SCLK);
static I2cBus           i2cBus(I2C_NUM_0, PIN_SDA, PIN_SCL);
// Use a conservative clock rate for bring-up; the slave is a simple wire-based
// stream and this avoids overdriving the link on a breadboard or long wires.
static DataLogCollector collector(bus, CS_PINS, CS_COUNT, 250'000);

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

    collector.addTarget(&uartTarget);
    collector.addTarget(&oledTarget);
    collector.addTarget(&espLogTarget);

    ESP_LOGI(TAG, "SPI data-log collector up. Scanning %d CS pins every %u ms.",
             CS_COUNT, (unsigned)SCAN_PERIOD_MS);

    TickType_t lastStatusTicks = xTaskGetTickCount();

    while (true)
    {
        collector.scan();

        TickType_t now = xTaskGetTickCount();
        if ((now - lastStatusTicks) >= pdMS_TO_TICKS(STATUS_PERIOD_MS))
        {
            collector.logStatus();
            lastStatusTicks = now;
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
