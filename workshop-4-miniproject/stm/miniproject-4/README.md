# Workshop 4-3. Reading complex data: working with registers and libraries for I2C sensors

Using STM32

- Extend the code we wrote today as follows:
- Add an RTC to the circuit,
  - (NOT DONE) Log light readings together with timestamps
  - (NOT DONE) Connect a TTL-USB converter to the circuit
  - (NOT DONE) Try reading the data back from the EEPROM and print it to the serial monitor
  - (NOT DONE) (Optional) Leave the device on a windowsill, then read back the day's data and put it into a table

## Overview

An STM32F401 on the I2C1 bus serves four devices: it logs light readings (ADC)
from a photoresistor to an external EEPROM once an hour, and shows a cat face,
the RTC clock and the addresses of devices found on the bus on the OLED. The
main loop is non-blocking — each module fires on its own timer (`HAL_GetTick`).

- [Src/main.c](Src/main.c) — bootstrap only: CubeMX peripheral init, then a loop that polls each module
- [Src/light_archive.c](Src/light_archive.c) — hourly light log into the external EEPROM (`LightArchive_Poll`)
- [Src/display_ui.c](Src/display_ui.c) — the OLED frame: clock, bus scan, light, SPI rates (`DisplayUI_Poll`)
- [Src/i2c_bus.c](Src/i2c_bus.c) — freeing a bus held by a hung slave (9 clocks + STOP) and recovering it
- [Src/sensor_stream.c](Src/sensor_stream.c) — ADC(DMA)+RTC sampling into the SPI log stream
- [Src/log_emission.c](Src/log_emission.c) — SPI1 log-slave: protocol, ring buffer, circular TX DMA
- [Inc/config.h](Inc/config.h) — all settings (addresses, screen layout, periods) in one place
- [Inc/ds1307.h](Inc/ds1307.h) — DS1307 RTC (0x68): read/write time, `DS1307_ReadTimeString`
- [Inc/eeprom.h](Inc/eeprom.h) — logging to an AT24Cxx EEPROM (0x50) with a next-write pointer
- [Inc/adc.h](Inc/adc.h) — light measurement via ADC (raw value / percent)
- [Inc/i2c_scanner.h](Inc/i2c_scanner.h) — I2C bus scanner, `I2CScanner_ScanToString`
- [Inc/ssd1306.h](Inc/ssd1306.h) — SSD1306 OLED driver (0x3C): frame buffer + pixel
- [Inc/text_renderer.h](Inc/text_renderer.h) — 5x7 font on top of the display driver
- [Inc/cat.h](Inc/cat.h) — cat face from primitives (line/circle/triangle)

## Hardware: Tiny RTC I2C module

The RTC (0x68) and logging EEPROM (0x50) are the two chips on one "Tiny RTC"
breakout board:

- **DS1307** — I2C RTC IC; sec/min/hour/date/month/year with auto leap-year compensation
- **56 B NV-SRAM** — built into the DS1307, battery-backed
- **AT24C32 EEPROM** — 4 KB (32 Kb), separate chip on the same board, used here for light-log storage
- **Backup battery** — CR2032 coin cell keeps time/SRAM alive on power loss (budget boards ship without it installed)
- Also has a programmable square-wave output pin and auto power-fail switchover