# stm/miniproject-4 — STM32F401 app + SPI log-slave

STM32F401 firmware, an STM32CubeIDE (Eclipse) project. An I2C application (DS1307
RTC, SSD1306 OLED, external EEPROM light logging via ADC) that **also acts as the
SPI log-slave** for the ESP32-C3 master (see `../../espc3/`). Source comments are
in Ukrainian.

## Build

Open `stm/miniproject-4/` in STM32CubeIDE and build the `Debug` configuration
(device: STM32F401CCUX, linker `STM32F401CCUX_FLASH.ld`). Regenerate peripheral
init from `miniproject-4.ioc` via the CubeMX view. HAL/CMSIS under `Drivers/` is
generated — keep hand-written code inside the `/* USER CODE BEGIN/END */` guards
so it survives regeneration. Firmware settings (I2C addresses, log intervals,
layout) are centralized in `Inc/config.h`.

## Module layout

`main.c` is a bootstrap only: CubeMX peripheral init (`MX_*_Init`,
`SystemClock_Config`, `Error_Handler`) plus a non-blocking loop that calls one
`*_Poll()` per module. Application state and logic live in the modules — put new
code there, not in `main.c`:

- `display_ui.c` — owns the `Ssd1306` object and the frame (clock / I2C
  addresses / light / SPI rates); also drives the periodic bus scan that feeds
  the address line, and re-inits the display after a failed I2C exchange.
- `light_archive.c` — the hourly light byte into the external EEPROM, incl. the
  next-write pointer stored in the EEPROM itself.
- `i2c_bus.c` — bit-banged bus release (9 clocks + manual STOP) for a slave that
  hangs holding SDA, and the full recover (`DeInit` → release → `HAL_I2C_Init`).
  Called at boot before the first I2C use, and per frame while the OLED is down.
- `sensor_stream.c` — ADC(DMA)+RTC sampling into the SPI log stream.
- `log_emission.c` — the SPI log-slave (below).

Modules take the `I2C_HandleTypeDef *` at `*_Init()` rather than reaching for
`hi2c1`; only `sensor_stream.c` still uses `extern` handles (it owns TIM2/ADC
glue). Device drivers (`ssd1306`, `ds1307`, `eeprom`, `i2c_scanner`,
`text_renderer`, `adc`) are header-only `static inline` in `Inc/`.

## SPI log-slave

The SPI log-slave is **entirely in `Src/log_emission.c` + `Inc/log_emission.h`**
(the stream buffer, protocol, and all SPI1/DMA HAL glue live there, *not* in
`it.c`/`hal_msp.c`). `main.c` only `#include`s it, calls `LogEmission_Init()`
once, and `LogEmission_Add*()` to queue records.

SPI1 is a hardware-NSS slave on **PA4=NSS/CS, PA5=SCK, PA6=MISO, PA7=MOSI** (AF5,
mode 0), transmitting via free-running circular DMA. It was added by hand (not via
`.ioc`), and `HAL_SPI_MODULE_ENABLED` was turned on in `stm32f4xx_hal_conf.h` —
re-running CubeMX won't recreate this, so don't expect the `.ioc` to own it. The
protocol constants are duplicated (C port) at the top of `log_emission.c`; keep
them in sync with `../../espc3/src/LogProtocol.hpp`, the source of truth.

Wiring to the master: STM PA4/PA5/PA6/PA7 ↔ ESP CS/SCLK(6)/MISO(1)/MOSI(7), common
ground.
