Module 4.4 and 4-5. SPI: Fast data transfer for displays and memory cards (Master/Slave)

## Tasks
1. ON STM32
   1. Read from the BME280 (In our case it will be I2C): temperature (°C), humidity (%RH), pressure (hPa).
   2. Add three new fields to the existing OLED screen output (previous homework): T, RH, P (for example: T: 23.4°C RH: 45% P: 1013 hPa).
   3. Refresh the BME280 data at least once per second (a separate timer/timestamp is fine), without delay().
2. On ESP-C3
   1. Develop your own custom SPI data transfer protocol that transmits date, time, temperature, light level percentage, humidity and pressure.
   2. On the ESP32, implement reception of this data. Parse it into separate variables and print it in a nice format to the serial monitor.
   3. Output received data to serial port
   4. No SD card is connected to ESP-C3 - received output just only to Serial
3. MAKE ESP-C3 to be SLAVE and STM to be MASTER
   1. Use only synchronous communication (no circular DMA transferring or async transmissions) between ESP and STM
4. Wiring: reuse the wiring from ../workshop-4-miniproject (take that project as the starting point)
   1. Additionally, a BME280 will be added to the Tiny RTC and OLED on the same I2C bus

---

# Implementation

Two firmwares, built and flashed separately, joined by one SPI link and one
shared header.

| directory | board | role |
| --- | --- | --- |
| [`stm/workshop-4-4/`](stm/workshop-4-4/) | STM32F401CCU6 ("black pill") | I2C application + **SPI master** — reads every sensor, draws the OLED, sends the telemetry frame |
| [`espc3/`](espc3/) | ESP32-C3 | **SPI slave** — receives the frame, parses it, prints it to the serial monitor |
| [`protocol/`](protocol/) | — | [`telemetry_packet.h`](protocol/telemetry_packet.h): the wire format, **compiled by both** |

Roles are reversed from `../workshop-4-miniproject`, which this project starts
from: there the ESP32-C3 was the master polling STM32 log-slaves over
free-running circular DMA. Here the STM32 clocks, the ESP32 listens, and the
transfer is a single blocking `HAL_SPI_Transmit` — no DMA anywhere on the link.

## Wiring

Unchanged from the mini-project — the same four wires, plus the BME280 joining
the existing I2C bus. Reversing master/slave does **not** move MOSI and MISO:
those names describe the master's role, not the pin's owner, so MOSI still
meets MOSI. What changes is who drives what.

```
        STM32F401 (master)                     ESP32-C3 (slave)
   PA4  CS   ──────────────────────────────►  GPIO7  CS
   PA5  SCK  ──────────────────────────────►  GPIO4  SCLK
   PA7  MOSI ──────────────────────────────►  GPIO6  MOSI   ← the data
   PA6  MISO ◄──────────────────────────────  GPIO5  MISO   (unused, one-way)
        GND  ─────────────────────────────────  GND

   I2C1 bus (PB6 = SCL, PB7 = SDA), 100 kHz, four devices:
        0x3C  SSD1306 OLED 128x64
        0x50  AT24C32 EEPROM   ┐ both on the "Tiny RTC" breakout; the EEPROM is
        0x68  DS1307 RTC       ┘ present on the bus but unused by this firmware
        0x76  BME280           ← new (0x77 if the module's SDO is tied high;
                                 the driver probes both)

   PA0  photoresistor divider -> ADC1_IN0
   PC13 on-board LED — blinks on each frame sent
   GPIO8 on the ESP — blinks on each frame received
```

## The link

One frame per second, one CS assertion per frame, 32 bytes.

```
 CS  ‾‾‾‾‾‾╲______________________________╱‾‾‾‾‾‾‾‾‾   (low for the whole frame)
 SCK ______╱╲╱╲╱╲╱ ... 256 clocks ... ╱╲╱╲╱____________
 MOSI      │← A5 5A 01 1A │ 26 payload bytes │ crc ←│
```

Because the frame is fixed-size and CS brackets exactly one of them, the
receiver never has to scan for a packet boundary — the chip-select line *is*
the framing. That is the main simplification over the mini-project's
self-synchronising byte stream, and it is what makes a synchronous link
practical here.

### Frame layout

Little-endian, no padding (`__attribute__((packed))`, checked by a
`static_assert` on both ends).

| off | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 1 | `magic0` | `0xA5` |
| 1 | 1 | `magic1` | `0x5A` |
| 2 | 1 | `version` | `0x01` |
| 3 | 1 | `payloadLen` | `26` |
| 4 | 1 | `year` | 2000-based (26 = 2026) |
| 5 | 1 | `month` | 1..12 |
| 6 | 1 | `day` | 1..31 |
| 7 | 1 | `hour` | 0..23 |
| 8 | 1 | `minute` | 0..59 |
| 9 | 1 | `second` | 0..59 |
| 10 | 1 | `lightPct` | **light level percentage**, 0..100 |
| 11 | 1 | `flags` | which sources are fresh this second |
| 12 | 2 | `tempC100` | **temperature**, °C × 100, signed |
| 14 | 2 | `humidity100` | **humidity**, %RH × 100 |
| 16 | 4 | `pressurePa` | **pressure**, Pa (÷100 → hPa) |
| 20 | 2 | `lightRaw` | raw 12-bit ADC average, 0..4095 |
| 22 | 4 | `uptimeMs` | master's `HAL_GetTick()` at build time |
| 26 | 4 | `seq` | monotonic frame counter |
| 30 | 2 | `crc` | CRC-16/CCITT-FALSE over bytes 0..29 |

Design notes that are easy to get wrong and are worth stating:

- **Fixed size, not variable.** An ESP32 SPI slave transaction is armed with a
  length *before* the master starts clocking, so a variable-length frame would
  need either a worst-case-sized transaction or a two-transaction
  header/body handshake. Everything here is known at compile time.
- **Scaled integers, not floats.** Neither MCU has an FPU enabled in this
  build; `°C × 100` is exact, endian-trivial and half the width.
- **One header, not two mirrored copies.** The mini-project kept the wire
  format in `LogProtocol.hpp` and hand-mirrored it in `log_emission.c`, with a
  "keep the two in sync" comment doing the load-bearing work. Here
  `protocol/telemetry_packet.h` is on the include path of both builds, so a
  layout change cannot be applied to only one end.
- **`flags` distinguishes stale from wrong.** A sensor that failed this cycle
  keeps its previous value in the frame and clears its bit, so the receiver
  prints "last known" instead of a plausible-looking `0.0`.

## What each end does

### STM32 — `stm/workshop-4-4/`

Non-blocking main loop, one `*_Poll()` per module, no `HAL_Delay()` anywhere
after boot:

- `sensor_stream.c` — the only code that touches a sensor. Light via ADC1 +
  TIM2 + circular DMA (averaged 20×/s), the DS1307 once a second, the **BME280
  once a second**. The BME280 runs in NORMAL mode with a 500 ms standby, so a
  read is one 8-byte I2C burst against an already-finished measurement — there
  is no wait-for-conversion in the loop.
- `telemetry_link.c` — builds the frame from those published values and clocks
  it out. 32 bytes at 1 MHz is ~256 µs of blocking, two orders of magnitude
  under the OLED flush that shares the same loop.
- `display_ui.c` — the OLED frame, now six lines including the three new ones.
- `bme280.h` — header-only driver: probes 0x76 then 0x77, verifies the chip ID
  (0x60 — a pin-compatible BMP280 reports 0x58 and has no humidity), reads both
  calibration blocks and runs Bosch's integer compensation.

Screen:

```
        12:34:56              <- DS1307, scale 2
   0x3C 0x50 0x68 0x76        <- I2C bus scan (every 10 s)
      T:23.4C RH:45%          <- NEW, BME280
        P:1013 hPa            <- NEW, BME280
        L1003 61%             <- light, raw + percent
       TX120 F0 E0            <- frames sent / SPI failures / OLED I2C failures
```

`TX` counts frames the master *sent*. The link is one-way, so it cannot know
whether the slave was armed to receive them — gaps only show up on the ESP32's
console, as jumps in the sequence number.

### ESP32-C3 — `espc3/`

`spi_slave` with **`SPI_DMA_DISABLED`**: without DMA the driver works straight
out of the hardware FIFO, which caps a transaction at 64 bytes on the C3. The
frame is 32, so the cap costs nothing and the data path loses its descriptors,
cache-alignment rules and 4-byte length rounding along with the DMA.

The one subtlety of an SPI slave is *arming*: the hardware receives into a
buffer the driver was given in advance, so a frame that arrives while nothing
is armed is not captured at all. `SpiSlave::receive()` therefore queues one
transaction and then waits for it; if the wait times out the transaction stays
armed and the caller loops straight back onto the same one, instead of stacking
a second and third transaction behind it the way a bare `spi_slave_transmit()`
in a loop would.

Serial output, once per second:

```
+-- frame #120 ---------------- master uptime 121043 ms --+
  date           2026-08-08
  time           14:32:07
  temperature    23.42 C
  humidity       45.31 %RH
  pressure       1013.25 hPa
  light          61 %   (raw 2497 / 4095)
+-- ok 120 / bad 0 / missed 0 -------------------+
```

`bad` (bytes arrived and failed validation — wiring, clock rate, version skew)
and `missed` (a hole in the sequence numbers — nothing arrived at all) are
counted apart, because they are different problems with different fixes.

## Build and flash

**ESP32-C3** — PlatformIO CLI lives at `~/.platformio/penv/Scripts/pio.exe`
(not on PATH). From inside `espc3/`:

```sh
pio run                 # build
pio run -t upload       # flash
pio device monitor      # USB console @ 115200 — this is where the frames print
```

**STM32** — open `stm/workshop-4-4/` in STM32CubeIDE and build `Debug` (device
STM32F401CCUX). The build needs the include path `../../../protocol`, which is
already in `.cproject`. `workshop-4-4.ioc` matches the code, so re-running
CubeMX will not silently put SPI1 back into slave mode.

Verified: `pio run` succeeds; all `Src/*.c` compile clean under
`arm-none-eabi-gcc -Wall -Wextra`; the shared header round-trips on the host
(seal → parse, single-bit flip caught by the CRC, `sizeof(TelemetryFrame) == 32`).
Not verified: on-hardware behaviour — neither board was connected while this
was written.

## Things worth knowing before touching this

- **`SPI_NSS_SOFT` is not optional.** Hardware NSS output on the F4 toggles CS
  between *bytes*, not around the transfer — the slave would see 32
  single-byte transactions instead of one 32-byte frame, and the framing
  described above would collapse.
- **CS is parked high in `MX_GPIO_Init`**, before `HAL_SPI_Init`. A floating CS
  is what corrupted the shared bus in the mini-project; the ESP end adds a
  pull-up for the window before the master's GPIO init has run.
- **1 MHz is a deliberate ceiling, not a maximum.** The STM32 runs on HSI with
  no PLL, so PCLK2 is 16 MHz and the prescaler is /16. Going faster means
  enabling the PLL, which shifts TIM2's ADC pacing and the I2C timing with it.
- **`ctrl_hum` must be written before `ctrl_meas`** on the BME280 — the chip
  only applies the humidity oversampling on the next `ctrl_meas` write. Get it
  backwards and humidity reads as a constant, which looks like a working sensor
  with strange values.
