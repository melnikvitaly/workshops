# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository shape

Workshop 4.4 / 4.5. **Two independent firmware projects** that talk to each
other over SPI but are built and flashed separately, plus one directory that
belongs to neither:

- **`stm/workshop-4-4/`** — STM32F401 firmware, an STM32CubeIDE (Eclipse)
  project. The I2C application (DS1307 RTC, SSD1306 OLED, BME280, light ADC)
  **and the SPI master**. Source comments are in Ukrainian. See
  [`stm/workshop-4-4/CLAUDE.md`](stm/workshop-4-4/CLAUDE.md).
- **`espc3/`** — ESP32-C3 firmware, the **SPI slave** / telemetry receiver.
  PlatformIO + ESP-IDF, C++, comments in English. See
  [`espc3/CLAUDE.md`](espc3/CLAUDE.md).
- **`protocol/`** — `telemetry_packet.h`, the wire format. **Not a copy in
  either project: both builds put it on their include path** (STM32 via
  `../../../protocol` in `.cproject`, ESP32 via `INCLUDE_DIRS` in
  `src/CMakeLists.txt`). Change the layout and both ends move together, or
  neither builds. Do not fork it back into per-project copies — the previous
  workshop did exactly that and the "keep the two in sync" comment was the only
  thing holding it together.

## The contract between the two ends

Read [`protocol/telemetry_packet.h`](protocol/telemetry_packet.h) first; it
documents the link model as well as the bytes. The short version, and the parts
that break silently if changed on one side only:

- STM32 is the **master** and the only talker. ESP32-C3 is the **slave** and
  only receives. This is reversed from `../workshop-4-miniproject`, which this
  project was copied from — don't carry that project's assumptions over.
- **Synchronous only.** One blocking `HAL_SPI_Transmit` on the master, one
  blocking `spi_slave_transmit(portMAX_DELAY)` on the slave. Nothing circular,
  nothing free-running, nothing pipelined; one transaction exists at a time.
  This is a workshop requirement, not an implementation detail — don't
  "optimise" it into a streaming design.
  - The slave *does* pass `SPI_DMA_CH_AUTO`, and must: `SPI_DMA_DISABLED` is
    broken on the C3 for anything past the first transaction. See
    [`espc3/CLAUDE.md`](espc3/CLAUDE.md) for the driver-level reason. It is
    still one blocking transaction — the DMA is internal to a call we are
    already waiting inside, and the master has no DMA at all.
- **One CS assertion = one 32-byte frame.** The CS line is the framing, which
  is why the receiver has no resync scanner. Anything that changes the frame
  size, or that lets CS toggle mid-frame (e.g. switching the master to hardware
  NSS output), breaks reception completely rather than degrading it.
- Mode 0, MSB first, 1 MHz. The clock ceiling is the STM32's 16 MHz PCLK2 (HSI,
  no PLL), not the ESP32.

## Verification

There is no test suite on either side. What can be checked without hardware:

```sh
# ESP32-C3 — from inside espc3/
~/.platformio/penv/Scripts/pio.exe run

# STM32 — no CubeIDE needed for a compile check
GCC='C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin/arm-none-eabi-gcc.exe'
cd stm/workshop-4-4 && for f in Src/*.c; do "$GCC" -mcpu=cortex-m4 -mthumb -std=gnu11 \
  -DUSE_HAL_DRIVER -DSTM32F401xC -Wall -Wextra -c "$f" -o /dev/null \
  -IInc -I../../protocol -IDrivers/STM32F4xx_HAL_Driver/Inc \
  -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy \
  -IDrivers/CMSIS/Device/ST/STM32F4xx/Include -IDrivers/CMSIS/Include; done
```

Both are expected to be warning-free. `protocol/telemetry_packet.h` also
compiles and round-trips on the host with plain `g++` — it depends on nothing
but `<stdint.h>`, so a seal/parse test needs no MCU headers.

Beyond that, verification is on-device: the OLED frame on one end and
`pio device monitor` on the other.
