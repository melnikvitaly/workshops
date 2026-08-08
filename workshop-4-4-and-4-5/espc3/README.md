# ESP32-C3 — SPI slave telemetry receiver

Receives one 32-byte telemetry frame per second from the STM32F401 master over
SPI, parses it into separate variables, and prints it to the serial monitor.
Nothing else is attached — no SD card, no OLED, no UART sink.

- [src/main.cpp](src/main.cpp) — pins, the receive loop, error reporting
- [src/hardware/SpiSlave.hpp](src/hardware/SpiSlave.hpp) — `spi_slave` wrapper, one blocking transaction at a time
- [src/TelemetryPrinter.hpp](src/TelemetryPrinter.hpp) — unpacks a frame into named fields and formats the output
- [src/hardware/StatusLed.hpp](src/hardware/StatusLed.hpp) — non-blocking activity blink on GPIO8
- [../protocol/telemetry_packet.h](../protocol/telemetry_packet.h) — the wire format, shared with the STM32

## Wiring

```
   ESP32-C3               STM32F401
   GPIO7  CS    ◄──────── PA4  (GPIO output)
   GPIO4  SCLK  ◄──────── PA5  (SPI1_SCK)
   GPIO6  MOSI  ◄──────── PA7  (SPI1_MOSI)    ← the data
   GPIO5  MISO  ────────► PA6  (SPI1_MISO)    unused, one-way link
   GND          ───────── GND
```

Mode 0 (CPOL=0, CPHA=0), MSB first, 1 MHz, master-paced.

## Output

```
=====================================================
 ESP32-C3 telemetry receiver  (SPI SLAVE)
=====================================================
 master : STM32F401 (SPI1, mode 0, 1 MHz)
 wiring : MOSI=GPIO6  MISO=GPIO5  SCLK=GPIO4  CS=GPIO7
 frame  : 32 bytes, one per CS assertion, ~1 per second
 carries: date, time, temperature, light %, humidity, pressure
=====================================================
waiting for the first frame...

+-- frame #1 ---------------- master uptime 1043 ms --+
  date           2026-08-08
  time           14:32:07
  temperature    23.42 C
  humidity       45.31 %RH
  pressure       1013.25 hPa
  light          61 %   (raw 2497 / 4095)
+-- ok 1 / bad 0 / missed 0 -------------------+
```

Three counters, three different failures:

| counter | meaning | usual cause |
| --- | --- | --- |
| `bad` | bytes arrived, validation failed | wiring, clock rate, the two ends built from different protocol versions |
| `missed` | a gap in the sequence numbers | the master sent while this end was not armed |
| *(nothing at all)* | after 5 s a `no frame for 5000 ms` warning | master not running, or CS/SCLK not connected |

A sensor that failed on the master side keeps its previous value in the frame
with its validity flag cleared, and prints as `(BME280 not answering)` or
`(RTC silent, last known)` rather than as a plausible zero.

## Build

```sh
~/.platformio/penv/Scripts/pio.exe run           # build
~/.platformio/penv/Scripts/pio.exe run -t upload # flash
~/.platformio/penv/Scripts/pio.exe device monitor
```
