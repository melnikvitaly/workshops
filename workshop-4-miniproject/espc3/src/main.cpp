#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <cstring>
#include <cstdio>
#include <driver/gpio.h>
#include "hardware/SpiBus.hpp"
#include "hardware/I2cBus.hpp"
#include "hardware/StatusLed.hpp"
#include "logs/UartTarget.hpp"
#include "logs/OledTarget.hpp"
#include "logs/EspLogTarget.hpp"
#include "logs/SdCardTarget.hpp"
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

// SD card, riding the same shared MOSI/MISO/SCLK as the log-slave bus (see
// bus.init() below) with its own dedicated CS. GPIO3 is the only pin left free
// once SPI (4/5/6), the log-slave CS (7), the LED (8), I2C (9/10) and UART1 (0)
// are assigned.
static constexpr gpio_num_t PIN_SD_CS = GPIO_NUM_3;
static constexpr const char* SD_MOUNT_POINT = "/sdcard";

static SpiBus           bus(HOST, PIN_MOSI, PIN_MISO, PIN_SCLK);
static I2cBus           i2cBus(I2C_NUM_0, PIN_SDA, PIN_SCL);
// SPI clock. Bring-up used a low rate for signal-integrity margin; with the SCK
// series resistor damping ringing, 2 MHz still has margin (~2 ms per 512-byte
// block). Step back to 1 MHz — or 250-500 kHz — if bit-slip or drops reappear;
// this rig has had a bad-wire failure before, and the symptom of pushing too far
// is CRC-rejected frames, which look like a silent drop in record rate.
//
// Upper bound is the STM32F401 slave, not this master. That board runs on HSI
// with no PLL, so PCLK2 = 16 MHz, and a SPI slave wants SCK comfortably below
// PCLK/4 = 4 MHz (rule of thumb — check the F401 datasheet's SPI slave timing
// before going past it). Going much faster means first enabling the PLL on the
// STM, which then shifts TIM2's ADC pacing and the I2C timing with it.
//
// Note this does NOT increase data throughput: the slave produces ~470 B/s and
// the master already drains ~125 KB/s, so a faster clock only re-reads the same
// ring more often. It buys latency and safety margin, not records.
static DataLogCollector collector(bus, CS_PINS, CS_COUNT, 2'000'000);

// Brief non-blocking activity blink each time the master clocks a block of bytes.
// activeHigh=false: the C3 on-board LED lights when the pin is driven low.
static StatusLed        spiLed(PIN_SPI_LED, /*pulseMs=*/15, /*activeHigh=*/false);

// Log sinks. The collector fans every record out to all of them, so they run
// side by side.
static UartTarget   uartTarget(UART_NUM_1, GPIO_NUM_0, 115200);
static OledTarget   oledTarget(i2cBus, 0x3C);
static EspLogTarget espLogTarget;   // prints records to the ESP-IDF console log
static SdCardTarget sdCardTarget(HOST, PIN_SD_CS, SD_MOUNT_POINT, "datalog.txt");

static const char* TAG = "LOG_MASTER";

// Drive every log-slave CS pin high (deselected) the moment the bus comes up,
// before any SpiDevice ever attach()es to it. Slaves like the STM32 use
// hardware NSS (see stm32f4xx_hal_msp.c: SPI_NSS_HARD_INPUT on PA4/CS7) — its
// MISO is only supposed to be enabled while NSS is low, but until the
// collector's first attach() takes over the pin for hardware SPI CS control,
// nothing drives it, so it floats. A floating hardware-NSS input can read as
// spurious select noise and make the slave drive MISO onto the shared bus at
// the wrong time, corrupting whatever else is transacting on it (e.g. the SD
// card, which is exactly what this fixes). attach() reconfigures the pin
// later anyway; this is just a safe placeholder in the meantime.
static void deselectCsPins()
{
    for (int i = 0; i < CS_COUNT; ++i)
    {
        gpio_set_direction(CS_PINS[i], GPIO_MODE_OUTPUT);
        gpio_set_level(CS_PINS[i], 1);
    }
}

// --- SD card bring-up debug (raw, bypassing SdCardTarget) -----------------
// The exact sequence that worked the one time this was confirmed working:
// mount once directly via esp_vfs_fat_sdspi_mount (no class in between),
// print card info, then write+read a counter line in a loop forever. Used to
// re-check whether the physical setup still behaves the same now that
// SdCardTarget consistently fails to mount. Never returns — this is
// throwaway bring-up code called instead of the rest of app_main.
// Root cause found (floating CS7/NSS letting the STM drive MISO onto the
// shared bus, fixed by deselectCsPins() below) — kept here, disabled, in case
// SD mounting needs to be re-isolated from SdCardTarget again.
#if 0
static void sdCardDebugTest()
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = HOST;

    sdspi_device_config_t slotCfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slotCfg.host_id = HOST;
    slotCfg.gpio_cs = PIN_SD_CS;

    esp_vfs_fat_mount_config_t mountCfg = {};
    mountCfg.format_if_mount_failed = false;
    mountCfg.max_files              = 5;
    mountCfg.allocation_unit_size   = 16 * 1024;

    sdmmc_card_t* card = nullptr;
    esp_err_t r = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slotCfg, &mountCfg, &card);
    if (r != ESP_OK)
    {
        ESP_LOGE(TAG, "SD mount failed on CS%d: %s", (int)PIN_SD_CS, esp_err_to_name(r));
        return;
    }
    sdmmc_card_print_info(stdout, card);

    char path[64];
    snprintf(path, sizeof(path), "%s/test.txt", SD_MOUNT_POINT);

    for (uint32_t count = 0;; ++count)
    {
        FILE* f = fopen(path, "w");
        if (!f)
        {
            ESP_LOGE(TAG, "SD write failed: could not open %s", path);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        fprintf(f, "hello from ESP32-C3 #%u, uptime=%u ms\n",
                (unsigned)count, (unsigned)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        fclose(f);

        f = fopen(path, "r");
        if (!f)
        {
            ESP_LOGE(TAG, "SD read-back failed: could not open %s", path);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        char line[64] = {};
        fgets(line, sizeof(line), f);
        fclose(f);

        ESP_LOGI(TAG, "SD card OK: wrote+read back %s: %s", path, line);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

extern "C" void app_main()
{
    // Give the native USB CDC (USB-Serial/JTAG) time to re-enumerate after the
    // reset that just brought us here, and you time to reattach `pio device
    // monitor` — without this, the monitor's old handle is dead and the boot
    // log below is gone before you can reconnect.
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_ERROR_CHECK(bus.init());
    deselectCsPins();
    // sdCardDebugTest();   // raw SD bring-up test, see #if 0 block above
    ESP_ERROR_CHECK(i2cBus.init());
    ESP_ERROR_CHECK(spiLed.init());
    collector.setStatusLed(&spiLed);

    collector.addTarget(&uartTarget);
    collector.addTarget(&oledTarget);
    //collector.addTarget(&espLogTarget);
    collector.addTarget(&sdCardTarget);

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
