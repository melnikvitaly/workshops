# Interfaces — `AIM` (ESP32-S3)

Pin map and low-level interface configuration for the `AIM` node. Wire formats
on UART1 are in [`protocol.md`](./protocol.md).

This is the authoritative pin map. `firmware/aim/src/Pinout.hpp` is its
implementation and the two must be kept in step.

---

## 1. Pin map

Module: **ESP32-S3-WROOM-1**, quad-flash / **no octal PSRAM** (`N4` or `N8`,
not `N8R8`) — an `N8R8` module uses GPIO33–37 for PSRAM, breaking
`SERVO_PAN` / `SERVO_TILT`. If an octal part is substituted, move the servos
to GPIO21 / 38 and update this table.

| Signal | GPIO | Mode | Notes |
|---|---|---|---|
| `SERVO_PAN` | 35 | LEDC out | Servo A, horizontal |
| `SERVO_TILT` | 36 | LEDC out | Servo B, vertical |
| `LASER_GATE` | 6 | out, pull-up | MOSFET gate. External pull-up required |
| `SD_CS` | 10 | SPI2 out | FSPICS0 — IOMUX |
| `SD_MOSI` | 11 | SPI2 out | FSPID — IOMUX |
| `SD_SCK` | 12 | SPI2 out | FSPICLK — IOMUX |
| `SD_MISO` | 13 | SPI2 in | FSPIQ — IOMUX |
| `OLED_SDA` | 15 | I²C0 open-drain | External 4.7 kΩ pull-up to 3V3 |
| `OLED_SCL` | 16 | I²C0 open-drain | External 4.7 kΩ pull-up to 3V3 |
| `LINK_TX` | 17 | UART1 out | To the USB-TTL adapter's RX |
| `LINK_RX` | 18 | UART1 in | From the USB-TTL adapter's TX |
| `BTN_ESTOP` | 4 | in, pull-up, **ISR** | RC 10 kΩ / 100 nF |
| `BTN_MODE` | 5 | in, pull-up | RC 10 kΩ / 100 nF. Polled at 50 Hz by `ui` |
| `BTN_CONTROL` | 7 | in, pull-up | RC 10 kΩ / 100 nF. Arm / fault-acknowledge |
| `VBAT_SENSE` | 2 | ADC1_CH1 | 2:1 divider, 100 kΩ / 100 kΩ + 100 nF |
| `SCOPE` | 47 | out | Toggled across the control step for the logic analyser |
| `STATUS_LED` | 48 | RMT out | WS2812 |
| `BOOT_BTN` | 0 | in | On-module. Strapping — boot select only, no runtime use |

**Free and uncommitted:** 1, 9, 14, 21, 33, 34, 37, 38.

**Reserved, do not use:** 3 / 45 / 46 (strapping), 19 / 20 (USB D−/D+),
39–42 (JTAG), 43 / 44 (UART0 console), 26–32 (SPI flash).

**No `SD_CD`:** GPIO14 was reserved for card-detect; the full-size SD socket
is wired without its card-detect / write-protect switches, so it's free.

---

## 2. UART1 — the `EYE` link

| | |
|---|---|
| **Port** | `UART_NUM_1`, GPIO17 TX / GPIO18 RX |
| **Speed** | 115200 8N1, no flow control |
| **Buffers** | RX 1024 B, TX 512 B, driver event queue depth 16 |
| **Carries** | Control ASCII and NDJSON — see [`protocol.md`](./protocol.md) |
| **On failure** | 300 ms without any frame on the selected channel (`valid`/`targetVisible` does not matter — silence is what counts) → `LINK_LOST`: axes stop, PIDs reset, laser off |

---

## 3. I²C0 — OLED

| | |
|---|---|
| **Port** | `I2C_NUM_0`, SDA GPIO15 / SCL GPIO16 |
| **Speed** | 400 kHz, fast mode |
| **Device** | SSD1306 128×64, address `0x3C` (`0x3D` if module jumper moved) |
| **Pull-ups** | 4.7 kΩ to 3V3, on the board — not on the module |
| **On failure** | Disable `ui` task, keep controlling; status LED blinks channel ordinal |

---

## 4. SPI2 — SD card

A **full-size SD card** is wired directly to the ESP32-S3 in **SPI mode**.
No adapter module, no level shifter: the card runs at 3V3.

### 4.1 Card pinout

Numbers are the pins of the full-size SD card, counted from the notched
corner.

| Card pin | SD name | SPI-mode name | ESP32-S3 | Notes |
|---|---|---|---|---|
| 1 | `DAT3` | `CS` | GPIO10 (`SD_CS`) | 10 kΩ pull-up to 3V3 |
| 2 | `CMD` | `MOSI` | GPIO11 (`SD_MOSI`) | 10 kΩ pull-up to 3V3 |
| 3 | `VSS1` | `GND` | GND | |
| 4 | `VDD` | `3V3` | 3V3 | 100 nF + 10 µF close to the pin |
| 5 | `CLK` | `SCK` | GPIO12 (`SD_SCK`) | |
| 6 | `VSS2` | `GND` | GND | |
| 7 | `DAT0` | `MISO` | GPIO13 (`SD_MISO`) | 10 kΩ pull-up to 3V3 |
| 8 | `DAT1` | — | not connected | 10 kΩ pull-up to 3V3 |
| 9 | `DAT2` | — | not connected | 10 kΩ pull-up to 3V3 |

- Pull-ups keep the lines defined when the card is missing or idle.
- Pins 8 and 9 are unused in SPI mode. Pull them up so the card never
  sees a floating line.
- Card-detect and write-protect switches of the socket are not wired.

### 4.2 Driver setup

| | |
|---|---|
| **Host** | `SPI2_HOST` (FSPI), IOMUX pins 10–13 |
| **Speed** | 400 kHz during card init, then 20 MHz |
| **Mount** | `esp_vfs_fat_sdspi_mount`, FAT32, `max_files = 2` |
| **DMA** | `SPI_DMA_CH_AUTO` at `spi_bus_initialize` |

**On failure** — logs once, raises an OLED flag, never stops the control loop:

| Condition | Response |
|---|---|
| No card at boot | `sd.present = 0`, retry mount on a 5 s timer |
| Removed while running | close file, one remount attempt, then `sd.present = 0` |
| Card full | `sd.full = 1`, stop writing, keep running |
| Write error | `sd.write_errors++`, remount once, then degrade |

---

## 5. LEDC — servo PWM

| | |
|---|---|
| **Timer** | `LEDC_TIMER_0`, `LEDC_LOW_SPEED_MODE` |
| **Frequency** | 50 Hz |
| **Resolution** | `LEDC_TIMER_16_BIT` |
| **Channels** | `LEDC_CHANNEL_0` pan, `LEDC_CHANNEL_1` tilt |
| **Pulse range** | 500–2500 µs, clamped before conversion |

Duty is written with `ledc_set_duty` + `ledc_update_duty` from `ctrl` only.

---

## 6. USB — console and flash

Native USB on GPIO19/20, differential pair. UART0 on GPIO43/44 is the
ESP-IDF console (`ESP_LOG` output only). **No data ever crosses UART0.**

---

## 7. Laser gate

Gate pin is undriven from power-on until firmware configures it. Firmware
calls `gpio_set_level()` to the inactive level **before** `gpio_config()`, and
enables the internal pull-up.

Interlock (in `safety`): beam may be lit only when state is `ARMED`, link is
fresh, WDT is healthy, no E-stop latched, and beam is requested. Every denial
is logged with its reason.
