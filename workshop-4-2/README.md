# Workshop 4-2. I²C: Синхронний послідовний інтерфейс із адресацією (TWI)

## Solution

ESP32-C3 (ESP-IDF) firmware driving three devices on one I²C bus. `app_main`
([src/main.cpp](src/main.cpp)) inits the bus once, then each frame reads the RTC,
scans the bus (every 10 s), and renders an animated cat with an `HH:MM:SS` clock
and the addresses of found devices.

Main classes (header-only drivers in `src/`):

- **`Ssd1306`** ([ssd1306.hpp](src/ssd1306.hpp)) — OLED driver: init sequence,
  1-bit framebuffer (`drawPixel`), `flush()` to the panel.
- **`Ds1307`** ([ds1307.hpp](src/ds1307.hpp)) — RTC driver: BCD read/write,
  `readTimeString()`.
- **`I2cScanner`** ([i2c_scanner.hpp](src/i2c_scanner.hpp)) — pings addresses
  1..126, builds the address string.
- **`TextRenderer`** ([text_renderer.hpp](src/text_renderer.hpp)) — 5x7 font text
  over the framebuffer.
- **`Cat`** ([cat.hpp](src/cat.hpp)) — draws/animates the cat via Bresenham
  primitives.
- **`config`** ([config.hpp](src/config.hpp)) — pins, addresses, layout, timing.

`TextRenderer` and `Cat` use only the public `Ssd1306` API, keeping the driver
minimal.

## Завдання
Ознайомитися з документацією на компоненти для практичного заняття
OLED  дисплей SSD1306
https://files.waveshare.com/upload/a/af/SSD1306-Revision_1.1.pdf

RTC DS1307
https://eu.mouser.com/datasheet/3/1014/1/DS1307.pdf
