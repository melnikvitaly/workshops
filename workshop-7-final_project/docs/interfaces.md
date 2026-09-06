# Interfaces — `AIM` (ESP32-S3)

Every interface on the `AIM` node: the pins it uses, the speed it runs at, why it
was chosen over the alternative, and what happens when it fails. The wire formats
carried over UART1 are in [`protocol.md`](./protocol.md); the system design is in
[`architecture.md`](./architecture.md).

This is the authoritative pin map. `firmware/aim/src/Pinout.hpp` is its
implementation and the two must be kept in step.

---

## 1. Pin map

Module: **ESP32-S3-WROOM-1**, quad-flash / **no octal PSRAM** (`N4` or `N8`, not
`N8R8`). The distinction is load-bearing — see §1.2.

| Signal | GPIO | Mode | Notes |
|---|---|---|---|
| `SERVO_PAN` | 35 | LEDC out | Servo A, below, horizontal |
| `SERVO_TILT` | 36 | LEDC out | Servo B, above, vertical |
| `LASER_GATE` | 6 | out, pull-up | MOSFET gate. **External pull-up required** — §7 |
| `SD_CS` | 10 | SPI2 out | FSPICS0 — IOMUX |
| `SD_MOSI` | 11 | SPI2 out | FSPID — IOMUX |
| `SD_SCK` | 12 | SPI2 out | FSPICLK — IOMUX |
| `SD_MISO` | 13 | SPI2 in | FSPIQ — IOMUX |
| `SD_CD` | 14 | in, pull-up | Card detect, socket switch. **Decision pending — §4.1** |
| `OLED_SDA` | 15 | I²C0 open-drain | External 4.7 kΩ pull-up to 3V3 |
| `OLED_SCL` | 16 | I²C0 open-drain | External 4.7 kΩ pull-up to 3V3 |
| `LINK_TX` | 17 | UART1 out | To the USB-TTL adapter's RX |
| `LINK_RX` | 18 | UART1 in | From the USB-TTL adapter's TX |
| `BTN_ESTOP` | 4 | in, pull-up, **ISR** | RC 10 kΩ / 100 nF. The only button on an interrupt |
| `BTN_MODE` | 5 | in, pull-up | RC 10 kΩ / 100 nF. Polled at 50 Hz by `ui` |
| `BTN_CONTROL` | 7 | in, pull-up | RC 10 kΩ / 100 nF. Arm / fault-acknowledge |
| `VBAT_SENSE` | 2 | ADC1_CH1 | 2:1 divider, 100 kΩ / 100 kΩ + 100 nF |
| `SCOPE` | 47 | out | Toggled across the control step for the logic analyser ⟦6.1⟧ |
| `STATUS_LED` | 48 | RMT out | WS2812 |
| `BOOT_BTN` | 0 | in | On-module. Strapping — boot select only, no runtime use |

**Free and uncommitted:** 1, 9, 21, 33, 34, 37, 38.

**Reserved, do not use:** 3 / 45 / 46 (strapping), 19 / 20 (USB D−/D+),
39–42 (JTAG), 43 / 44 (UART0 console), 26–32 (SPI flash).

### 1.1 Why these pins

- **SD on 10–13 is not arbitrary.** Those are the ESP32-S3's IOMUX pins for
  SPI2/FSPI. Routed through IOMUX the bus bypasses the GPIO matrix and its
  added delay, which is what lets the clock run high at all; any other four pins
  route through the matrix, cap the usable clock and put
  `sd.write_max_latency_us` at risk for no gain anywhere else.
- **UART1 on 17/18** because the pair is adjacent on the module edge, so the
  header routes as two parallel traces with a ground between them, and neither pin
  is a strapping pin — a USB-TTL adapter presenting a level at reset cannot change
  the boot mode.
- **`BTN_MODE` on 5** satisfies the non-strapping requirement in
  [`TASKS.md`](../TASKS.md) §2. Pressing it while the board resets must do
  nothing at all, which rules out 0, 3, 45 and 46.
- **Buttons on 4/5/7** sit on ADC1 channels and are used as plain digital inputs.
  That is deliberate: it keeps ADC2 — which is unusable whenever WiFi is on — out
  of the design entirely, so Phase 1 adds a radio without moving an input.
- **`VBAT_SENSE` on ADC1_CH1** for the same reason.
- **`SCOPE` on 47** is next to the status LED at the board edge, so a probe clip
  reaches it without a fixture.

### 1.2 The servo pins are conditional on the module

GPIO33–37 are free **only** on modules without octal PSRAM. On an `N8R8` the
octal PSRAM interface consumes 33–37, and `SERVO_PAN` / `SERVO_TILT` are broken
in a way no amount of firmware debugging will explain. `workshop-5-miniproject`
drives servos on 35/36 today, which is the evidence that the module on the bench
is a non-octal part.

**Verify the part number on the module before the board is ordered.** If an octal
part is ever substituted, move the servos to 21 and 38 and update this table
first.

---

## 2. UART1 — the `EYE` link

| | |
|---|---|
| **Port** | `UART_NUM_1`, GPIO17 TX / GPIO18 RX |
| **Speed** | 115200 8N1, no flow control |
| **Buffers** | RX 1024 B, TX 512 B, driver event queue depth 16 |
| **Carries** | Control ASCII and NDJSON — see [`protocol.md`](./protocol.md) |

**Why UART1 and not UART0.** UART0 is the console. Sharing it with the data link
is a known hazard: `ESP_LOG` text lands in the middle of the data stream, so a log
line beginning with `F` is indistinguishable from a fire command until the grammar
is hardened against it. Moving the link to UART1 removes the class of bug instead
of defending against it, and it keeps the console available for debugging *while*
the link is under test.

**Why 115200.** The control path is one short line per camera frame — roughly
24 bytes at 30–60 fps, well inside the budget. Telemetry is the traffic that can
saturate the wire, which is why it is rate-limited by `telemetry.rate_hz` rather
than free-running. Raising the baud is the first thing to try if `link.tx_dropped`
is ever non-zero, and nothing in the design prevents it; 115200 is chosen because
every USB-TTL adapter and every serial monitor agrees on it without configuration.

**Why no DMA.** ESP-IDF's UART driver already services the FIFO from an ISR into
a ring buffer. At 115200 the interrupt load is negligible, and DMA would add an
alignment constraint and a second copy to buy nothing measurable. Requirement
6.2 asks for judgement about DMA, and the honest answer is that the SD path is
where it pays (§4). *(This paragraph is the one [`TASKS.md`](../TASKS.md) §5
asks to be written down.)*

**On failure.** 300 ms without a valid frame on the selected channel →
`LINK_LOST`: axes stop, both PIDs reset, laser forced off. The port is not closed
and not reopened — a silent link and a live link are the same driver state, so
recovery is simply the next valid frame. Framing errors, overruns and break
conditions are counted as `link.uart_err` and are never fatal.

---

## 3. I²C0 — OLED

| | |
|---|---|
| **Port** | `I2C_NUM_0`, SDA GPIO15 / SCL GPIO16 |
| **Speed** | 400 kHz, fast mode |
| **Device** | SSD1306 128×64, address `0x3C` (`0x3D` if the module's jumper is moved) |
| **Pull-ups** | 4.7 kΩ to 3V3, on the board — not on the module |

**Why I²C and not SPI.** Two pins against four, on a board already tight around
the SD bus, for a display refreshed at 10 Hz. A full 128×64 frame is 1 KB, which
at 400 kHz takes about 21 ms — comfortably inside the `ui` task's 100 ms period.
`ui` is the lowest-priority task in the system, so that transfer costs the control
loop nothing.

**Why only one device on the bus.** A single device makes a NAK unambiguous: it
means the display, and nothing else. This is the same argument that declined the
DS3231 in favour of SNTP — a second device here turns every bus fault into a
diagnosis problem. See
[`architecture.md` §5](./architecture.md#5-configuration-and-storage).

**On failure.** Absent at boot or NAKing at runtime → log once, disable the `ui`
task, **keep controlling**. A missing display is a lost convenience, never a lost
gimbal. The `MODE` button still works with no display at all: the status LED
blinks the channel ordinal, which is the fallback the button exists for.

---

## 4. SPI2 — micro-SD

| | |
|---|---|
| **Host** | `SPI2_HOST` (FSPI), IOMUX pins 10–13 |
| **Speed** | 400 kHz during card init, then 20 MHz; raise only against measurement |
| **Mount** | `esp_vfs_fat_sdspi_mount`, FAT32, `max_files = 2` |
| **DMA** | `SPI_DMA_CH_AUTO` at `spi_bus_initialize` ⟦6.2⟧ |

**Why SPI and not SDMMC.** The 4-bit SDMMC peripheral is faster, but it ties the
card to a fixed pad group and needs pull-ups on every data line. The log is a few
kB/s against a bus that carries megabytes — the bottleneck is the card's internal
wear-levelling, not the wire, so the extra pins and constraints buy nothing.

**Why a card at all, rather than raw NOR flash.** The card is pulled out and read
on a PC. That is the whole argument, and it is also why the record format is CSV.

**Why DMA here and nowhere else in Phase 0.** SD writes are 4–8 KB blocks —
large, periodic, and issued from `logger` while `ctrl` has a 20 ms deadline to
meet. Without DMA the CPU copies every byte of every block.
**Confirm the driver takes a channel on the IDF version in use** — requirement 6.2
rests on it now that `PILOT` is Phase 1.

**On failure — the four named conditions.** Each logs once, raises an OLED status
flag, and **never stops the control loop**:

| Condition | Detected by | Response |
|---|---|---|
| No card at boot | mount fails | `sd.present = 0`, retry mount on a 5 s timer |
| Removed while running | `SD_CD` edge, or a write error | unmount, `sd.present = 0`, keep queueing, drop-oldest |
| Card full | short `f_write` / `f_getfree` | `sd.full = 1`, stop writing, keep running |
| Write error | `f_write` / `f_sync` return | `sd.write_errors++`, remount once, then degrade |

### 4.1 Decision pending — `SD_CD`

GPIO14 is reserved above for the socket's card-detect switch. Without it,
"removed while running" is detectable only *after* a failed write — which means
`sd.present` and `sd.mounted` carry the same information, and one of the two
telemetry fields in
[`architecture.md` §5](./architecture.md#5-configuration-and-storage) is
redundant. With it, removal is an edge and the log records the moment it
happened.

It costs one pin, a socket variant that has the switch, and one pull-up.
**Confirm the socket before the schematic is drawn** ([`TASKS.md`](../TASKS.md)
§8). If the part on hand has no CD switch, delete the row and record that here.

---

## 5. LEDC — servo PWM ⟦2.5⟧

| | |
|---|---|
| **Timer** | `LEDC_TIMER_0`, `LEDC_LOW_SPEED_MODE` (the S3 has no high-speed mode) |
| **Frequency** | 50 Hz — the standard hobby-servo frame |
| **Resolution** | `LEDC_TIMER_16_BIT` — 65536 steps across the 20 ms frame |
| **Channels** | `LEDC_CHANNEL_0` pan, `LEDC_CHANNEL_1` tilt |
| **Pulse range** | 500–2500 µs, clamped to the working zone before conversion |

16-bit is chosen over the wider setting the clock would allow because the
resulting step is already far finer than any hobby servo resolves; the remaining
bits would encode noise. Duty is written with `ledc_set_duty` + `ledc_update_duty`
from `ctrl` only — no other task touches these channels.

**`esp_timer` is used for measurement, not for control:** `esp_timer_get_time()`
brackets the PID step, the frame parse and the render. The control period itself
comes from `vTaskDelayUntil` in `ctrl`, so a long step shortens the following
delay instead of accumulating drift.

**On failure.** A hobby servo returns no feedback, so a dead servo is invisible
on this interface. It is caught instead by the camera loop failing to close — the
error vector stops shrinking — and by the boot zone tour, whose *direction* is the
geometry test ([`architecture.md` §6](./architecture.md#6-control)).

---

## 6. USB — console and flash

Native USB on GPIO19/20, routed as a 90 Ω differential pair
([`README.md`](../README.md) §3). UART0 on GPIO43/44 is the ESP-IDF console:
`ESP_LOG` output and nothing else. **No data ever crosses UART0.**

---

## 7. Laser gate — the boot-safe requirement

The gate pin is undriven from power-on until firmware configures it, and
`gpio_config()` enables the output *before* the first `gpio_set_level()` writes
a value. Both windows light the beam without being asked.

- **Firmware half, Phase 0:** call `gpio_set_level()` to the inactive level
  **before** `gpio_config()`, and enable the internal pull-up so the pre-`init()`
  window rests off. About five lines.
- **Hardware half, Phase 1:** an external pull-up on the MOSFET gate — the only
  fix that covers power-on through the bootloader, where no firmware is running
  to be correct.

The interlock is unconditional and lives in `safety`: the beam may be lit only
when the state is `ARMED`, the link is fresh, the WDT is healthy, no E-stop is
latched, and the beam is actually requested. Every denial is logged with its
reason.

---

## 8. Phase 1 interfaces — reserved, not designed here

`PILOT` over ESP-NOW, MQTT over WiFi, and the `AIM` ⟷ `VAULT` SPI link (with
`VAULT` as master and `AIM` as slave) are specified in
[`architecture.md` §3](./architecture.md#3-communications). None of them claims
a pin from §1 except the SPI slave link, which needs four pins plus `DRDY` —
21, 33, 34, 37 and 38 are held for it.
