#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_attr.h>
#include <esp_timer.h>
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

// How long the link may be silent before saying so. The master sends once a
// second, so a few seconds of silence is a real problem (wiring, or the master
// not running) and worth printing rather than hanging mutely forever.
//
// This runs on an esp_timer rather than as a receive timeout: the receive call
// blocks with portMAX_DELAY so that exactly one transaction is ever armed (see
// SpiSlave::receive), which leaves no timeout to hang the notice off.
static constexpr uint32_t QUIET_NOTICE_MS = 5000;

static SpiSlave         slave(HOST, PIN_MOSI, PIN_MISO, PIN_SCLK, PIN_CS, SPI_MODE);
static StatusLed        led(PIN_LED, /*pulseMs=*/25, /*activeHigh=*/false);
static TelemetryPrinter printer;

// The receive buffer. DMA_ATTR puts it in internal RAM, word-aligned — both are
// checked by spi_slave_queue_trans() and rejected outright otherwise. Static
// because the driver writes into it while receive() is not on the stack.
static DMA_ATTR uint8_t g_rx[TELEMETRY_FRAME_SIZE];

static const char* TAG = "TELEMETRY_SLAVE";

// Set by the receive loop on every completed transaction, read by the quiet
// timer. Only ever a whole 64-bit store from one task and a load from the timer
// task, so no lock — a torn read would at worst delay one notice by 5 s.
static volatile int64_t g_lastTransactionUs = 0;

// ---------------------------------------------------------------------------
// Raw dump of what the wire actually delivered.
//
// The bit count matters as much as the bytes: without DMA the driver copies out
// of the FIFO bounded by that count, so "3 bytes" means three bytes were copied
// and the other 29 are the zeros we cleared. A count that is not a whole number
// of bytes means the transaction ended mid-byte (a CS/clock timing problem); a
// whole-byte count that is simply too small means it ended early but cleanly.
// And if the leading bytes read A5 5A 01 1A, the frame *start* was captured
// correctly and only the tail is missing — which separates a framing fault from
// a wiring or SPI-mode fault.
// ---------------------------------------------------------------------------
static void dumpRaw(const char* why, size_t bits, const uint8_t* buf, size_t len)
{
    std::printf("  RAW  %-18s %u bits (%u bytes, %s):",
                why, (unsigned)bits, (unsigned)(bits / 8),
                (bits % 8) ? "NOT byte-aligned" : "byte-aligned");
    for (size_t i = 0; i < len; ++i)
        std::printf(" %02X", buf[i]);
    std::printf("\n");
    std::printf("  expect A5 5A 01 1A ... as the first four bytes\n");
}

static void quietTimerCb(void*)
{
    const int64_t last = g_lastTransactionUs;
    if (last != 0 && (esp_timer_get_time() - last) < (int64_t)QUIET_NOTICE_MS * 1000)
        return;
    ESP_LOGW(TAG, "no SPI activity for %u ms — is the STM32 master running, and are CS/SCLK wired?",
             (unsigned)QUIET_NOTICE_MS);
}

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

    // Periodic "the link has gone quiet" notice. It has to live here rather
    // than as a receive timeout, because the receive blocks forever to keep
    // exactly one transaction armed (see SpiSlave::receive).
    esp_timer_handle_t quietTimer = nullptr;
    esp_timer_create_args_t quietArgs = {};
    quietArgs.callback = &quietTimerCb;
    quietArgs.name     = "spi_quiet";
    ESP_ERROR_CHECK(esp_timer_create(&quietArgs, &quietTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(quietTimer, (uint64_t)QUIET_NOTICE_MS * 1000));

    while (true)
    {
        size_t    bits = 0;
        esp_err_t r    = slave.receive(g_rx, sizeof(g_rx), bits);

        if (r != ESP_OK)
        {
            ESP_LOGE(TAG, "spi slave receive failed: %s", esp_err_to_name(r));
            continue;
        }

        g_lastTransactionUs = esp_timer_get_time();
        led.pulse();

        if (bits != TELEMETRY_FRAME_SIZE * 8)
        {
            // The master clocked a different number of bits than one frame.
            // Without DMA the driver copied only this many bits out of the
            // FIFO, so the buffer really is short — dump it rather than guess,
            // because the leading bytes say whether the frame *start* was
            // captured (framing fault) or whether nothing recognisable arrived
            // (wiring / SPI-mode fault).
            printer.countBad();
            ESP_LOGW(TAG, "short transaction: %u of %d bits",
                     (unsigned)bits, TELEMETRY_FRAME_SIZE * 8);
            dumpRaw("short", bits, g_rx, sizeof(g_rx));
            continue;
        }

        TelemetryPayload payload;
        TelemetryStatus  status = Telemetry_Parse(g_rx, &payload);
        if (status != TELEMETRY_OK)
        {
            // Right length, wrong content: the clock and CS are behaving and
            // the bytes themselves are wrong. Different fault, same need for
            // the raw bytes.
            printer.countBad();
            ESP_LOGW(TAG, "frame rejected: %s", Telemetry_StatusName(status));
            dumpRaw(Telemetry_StatusName(status), bits, g_rx, sizeof(g_rx));
            continue;
        }

        printer.print(payload);
    }
}
