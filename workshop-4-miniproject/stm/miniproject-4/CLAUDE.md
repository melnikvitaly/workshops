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
