# stm/workshop-4-4 — STM32F401 app + SPI telemetry master

STM32F401 firmware, an STM32CubeIDE (Eclipse) project. An I2C application
(DS1307 RTC, SSD1306 OLED, BME280, light ADC) that is **also the SPI master**
for the ESP32-C3 slave (see `../../espc3/`). Source comments are in Ukrainian.

Nothing writes to the AT24C32 EEPROM. The chip is still on the Tiny RTC board
next to the DS1307 and still answers the bus scan at 0x50, so it appears on the
screen's address line — but the mini-project's hourly light archive
(`light_archive.c` / `eeprom.h`) has been removed and there is no EEPROM driver
in this project.

## Build

Open `stm/workshop-4-4/` in STM32CubeIDE and build the `Debug` configuration
(device: STM32F401CCUX, linker `STM32F401CCUX_FLASH.ld`). Regenerate peripheral
init from `workshop-4-4.ioc` via the CubeMX view. HAL/CMSIS under `Drivers/` is
generated — keep hand-written code inside the `/* USER CODE BEGIN/END */` guards
so it survives regeneration. Firmware settings (I2C addresses, screen layout,
periods) are centralized in `Inc/config.h`.

The build has one include path that is not part of the project: `../../../protocol`
(added to both configurations in `.cproject`). That is where `telemetry_packet.h`
lives — see the top-level `CLAUDE.md` for why it is not a local copy.

## Module layout

`main.c` is a bootstrap only: CubeMX peripheral init (`MX_*_Init`,
`SystemClock_Config`, `Error_Handler`) plus a non-blocking loop that calls one
`*_Poll()` per module. Application state and logic live in the modules — put new
code there, not in `main.c`:

- `sensor_stream.c` — **the only code that talks to a sensor.** Light (ADC1 +
  TIM2 + circular DMA, averaged), the DS1307, and the BME280, each on its own
  timer. Everything else reads the published values through
  `SensorStream_Latest*()`; nothing else opens an I2C exchange with a sensor.
  Keep it that way — it is what makes "is this reading fresh?" a single flag
  rather than a per-consumer question.
- `telemetry_link.c` — the SPI master (below).
- `display_ui.c` — owns the `Ssd1306` object and the six-line frame (clock /
  I2C addresses / T+RH / P / light / link counters); also drives the periodic
  bus scan that feeds the address line, and re-inits the display after a failed
  I2C exchange.
- `i2c_bus.c` — bit-banged bus release (9 clocks + manual STOP) for a slave that
  hangs holding SDA, and the full recover (`DeInit` → release → `HAL_I2C_Init`).
  Called at boot before the first I2C use, and per frame while the OLED is down.

Modules take the `I2C_HandleTypeDef *` at `*_Init()` rather than reaching for
`hi2c1`; only `sensor_stream.c` still uses `extern` handles (it owns the
TIM2/ADC glue). Device drivers (`ssd1306`, `ds1307`, `bme280`, `i2c_scanner`,
`text_renderer`, `adc`) are header-only `static inline` in `Inc/`.

`text_renderer.h` silently draws any character it has no glyph for as a space.
Adding a new string to the display means checking its characters are in the
table first — that is how the units vanished off the screen twice already.

## SPI telemetry master

Entirely in `Src/telemetry_link.c` + `Inc/telemetry_link.h`. `main.c` only
calls `TelemetryLink_Init()` once and `TelemetryLink_Poll()` from the loop.

SPI1 is a **master**, mode 0, MSB first, prescaler /16 → 1 MHz, on
**PA5=SCK, PA6=MISO, PA7=MOSI** (AF5). **PA4 is a plain GPIO output** used as
CS, brought up by `MX_GPIO_Init` (parked high before `HAL_SPI_Init` runs) and
driven by this module.

Three things about that configuration are load-bearing:

1. **`SPI_NSS_SOFT`.** Hardware NSS output on the F4 toggles CS between *bytes*
   rather than around the transfer. The receiver frames on CS, so hardware NSS
   would turn one 32-byte frame into 32 single-byte transactions.
2. **No DMA, no interrupts.** The transfer is one blocking `HAL_SPI_Transmit`.
   The mini-project's `hdma_spi1_tx` / `DMA2_Stream3` and its IRQ handler are
   gone from `hal_msp.c`, `it.c` and the `.ioc`. Synchronous-only is a workshop
   requirement.
3. **Role reversal.** This board was the SPI *slave* in
   `../../../workshop-4-miniproject`. The `.ioc` has been updated to match the
   code (`SPI1.Mode=SPI_MODE_MASTER`, `VirtualNSS=VM_NSSSOFT`, PA4 as
   `GPIO_Output`), so a CubeMX regeneration will not quietly put it back.

Wiring to the slave: STM PA4/PA5/PA6/PA7 ↔ ESP GPIO7 (CS) / GPIO4 (SCLK) /
GPIO5 (MISO) / GPIO6 (MOSI), common ground.

## BME280

`Inc/bme280.h`, header-only. Probes 0x76 then 0x77 (SDO low/high) so the module
does not have to be re-strapped, and checks the chip ID: 0x60 is a BME280,
0x58 is a pin-compatible BMP280 with no humidity sensor, and the driver refuses
the latter rather than reporting constant humidity.

Runs in NORMAL mode with a 500 ms standby against a 1 s poll, so a read is one
8-byte I2C burst against a finished measurement — no conversion wait in the
main loop, which is what the "no `delay()`" requirement is really about.

Two ordering constraints from the datasheet are easy to break:
`ctrl_hum` (0xF2) must be written **before** `ctrl_meas` (0xF4) or humidity
oversampling never takes effect, and temperature must be compensated **first**
because it produces the `t_fine` term the pressure and humidity formulas need.
