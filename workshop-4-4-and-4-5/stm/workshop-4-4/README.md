# STM32F401 — sensors, OLED, and the SPI telemetry master

An STM32F401 on one I2C1 bus, plus a photoresistor on the ADC, plus SPI1
clocking a telemetry frame to the ESP32-C3 once a second. The main loop is
non-blocking — each module fires on its own `HAL_GetTick` timer.

- [Src/main.c](Src/main.c) — bootstrap only: CubeMX peripheral init, then a loop that polls each module
- [Src/sensor_stream.c](Src/sensor_stream.c) — the only code that reads a sensor: light (ADC+DMA), DS1307, BME280
- [Src/telemetry_link.c](Src/telemetry_link.c) — SPI1 master: builds the 32-byte frame and clocks it out synchronously
- [Src/display_ui.c](Src/display_ui.c) — the OLED frame: clock, bus scan, T/RH, P, light, link counters
- [Src/i2c_bus.c](Src/i2c_bus.c) — freeing a bus held by a hung slave (9 clocks + STOP) and recovering it
- [Inc/config.h](Inc/config.h) — all settings (screen layout, periods, timeouts) in one place
- [Inc/bme280.h](Inc/bme280.h) — BME280 driver (0x76/0x77): calibration, NORMAL mode, integer compensation
- [Inc/ds1307.h](Inc/ds1307.h) — DS1307 RTC (0x68): read/write time
- [Inc/adc.h](Inc/adc.h) — light measurement via ADC (raw value / percent)
- [Inc/i2c_scanner.h](Inc/i2c_scanner.h) — I2C bus scanner, `I2CScanner_ScanToString`
- [Inc/ssd1306.h](Inc/ssd1306.h) — SSD1306 OLED driver (0x3C): frame buffer + pixel
- [Inc/text_renderer.h](Inc/text_renderer.h) — 5x7 font on top of the display driver
- [../../protocol/telemetry_packet.h](../../protocol/telemetry_packet.h) — the wire format, shared with the ESP32

## The screen

```
        12:34:56              DS1307, large
   0x3C 0x50 0x68 0x76        I2C bus scan, refreshed every 10 s
      T:23.4C RH:45%          BME280
        P:1013 hPa            BME280
        L1003 61%             light: raw ADC average and percent
       TX120 F0 E0            frames sent / SPI failures / OLED I2C failures
```

Six lines of a 64-row panel leaves room for only one double-height line, so the
clock keeps the large font and everything else is 5x7. The date is not shown —
the time is already there and there is no seventh row — but it *is* sent over
SPI.

## Hardware on the I2C bus

**Tiny RTC module** — two chips on one breakout:

- **DS1307** (0x68) — RTC; sec/min/hour/date/month/year with leap-year handling
- **56 B NV-SRAM** — inside the DS1307, battery-backed
- **AT24C32 EEPROM** (0x50) — 4 KB, separate chip. **Unused by this firmware.**
  The mini-project logged an hourly light byte into it; nothing here writes to
  it. It still answers the bus scan, so it still appears on the address line.
- **Backup battery** — CR2032 keeps time alive on power loss (budget boards ship without it)

**SSD1306** (0x3C) — 128x64 OLED.

**BME280** (0x76, or 0x77 if SDO is tied high) — temperature, humidity,
pressure. Added for this workshop; it is the fourth address on the same bus, so
it shows up in the scan line on the screen the moment it is plugged in.

⚠️ Modules sold as "BME280" are sometimes BMP280 — same footprint, same pinout,
no humidity sensor, chip ID 0x58 instead of 0x60. `BME280_Init` checks the ID
and refuses the BMP280 rather than reporting a constant humidity that looks
like a working sensor.
