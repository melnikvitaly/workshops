# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository shape

This is a workshop mini-project made of **two independent firmware projects** that are meant to talk to each other over SPI but are built and flashed separately:

- **`espc3/`** — ESP32-C3 firmware, the **SPI master**. PlatformIO + ESP-IDF, C++. This is the active development target.
- **`stm/miniproject-4/`** — STM32F401 firmware, an STM32CubeIDE (Eclipse) project. An I2C application (DS1307 RTC, SSD1306 OLED, external EEPROM light logging via ADC) that **also acts as the SPI log-slave** for the ESP master via the self-contained `Src/log_emission.c` module (see below). Source comments are in Ukrainian.

`openspec/`, `.claude/`, and `.github/` hold OpenSpec tooling/skills, not firmware.

## espc3 — build / flash / monitor

PlatformIO CLI lives at `~/.platformio/penv/Scripts/pio.exe` (not on PATH). Run from **inside `espc3/`**:

```sh
pio run                 # build (target: esp32-c3-devkitm-1)
pio run -t upload       # flash
pio device monitor      # console UART0 @ 115200
pio run -t clean
```

There is no test suite. Verification is a successful `pio run` plus on-device serial output.

## espc3 — architecture

An SPI **bus scanner / hot-plug collector**, not a fixed point-to-point link. `app_main` (`src/main.cpp`) sets up the bus and one or more log sinks, then loops `collector.scan()` every `SCAN_PERIOD_MS`. The design has five non-obvious invariants — preserve them when editing:

1. **Attach → probe → (drain) → detach, one device at a time** (`DataLogCollector::scan`). Each pass, for every predefined CS pin, the collector adds an SPI device, probes it, drains its logs, then removes it. Because only one device is ever on the bus at once, the number of scanned CS pins is *not* limited by the SPI host's concurrent-device cap. Don't "optimize" this into keeping devices attached without re-checking that cap.

2. **Presence is inferred from a valid framed response**, since SPI has no ACK. Frames are fixed 32-byte, both directions (`src/LogProtocol.hpp`). Response: `[0]=MAGIC [1]=status [2]=len [3..6]=recordId(u32) [7..10]=recordTimeMs(u32) [11..12]=dropped(u16) [13..30]=payload(18) [31]=checksum`. A device is "present" only if `logproto::valid()` passes. An empty CS line reads as 0xFF (MISO is held high by an internal pull-up set in `SpiBus::init`), which can never satisfy MAGIC+checksum. Fields are a **packed-struct overlay** (`logproto::Frame` union of `bytes`/`req`/`resp`) — access via `f.resp.recordId` etc., not offsets. This assumes little-endian (both MCUs are) and is guarded by `static_assert`s; the STM slave mirrors the same struct in `log_emission.c` and the two must stay in sync.

3. **Reliable delivery via cumulative ack (no separate ACK message).** Each `CMD_GET_LOG` request carries `lastReceivedId` (`REQ_LAST_ID`) — the highest record id already delivered. The slave marks everything `<= lastReceivedId` as processed and returns the oldest record with a greater id (or `ST_NO_DATA`). The master advances its per-device high-water mark (`_lastId[i]`) **only after a frame validates and is dispatched**, so a corrupt/lost response is simply re-requested at the same offset next pass — nothing is lost or double-sent. Requests are also checksummed (`logproto::sign`) so the slave never acts on a garbled offset. `_lastId[i]` is reset to 0 on the REMOVED→ADDED edge so a freshly plugged device isn't skipped, and lives only in RAM (a reboot restarts from 0). The only data-loss path is the slave's bounded buffer overflowing when the master is absent/too slow; the slave reports its monotonic overflow-drop count in `dropped`, which the master tracks in `_dropped[i]` and logs when it grows. See the slave-side contract block in `LogProtocol.hpp`.

4. **Time is master-as-reference.** The master is the sole time authority: `masterNowMs()` (ms since boot today) is pushed to each slave via `CMD_SET_TIME` on the ADDED edge and every `TIME_SYNC_MS`. Slaves stamp `recordTimeMs` on that timebase and set `FLAG_TIME_SYNCED` (bit 7 of the status byte — read status with `logproto::statusCode()`, the flag with `timeSynced()`); before sync they stamp raw uptime with the flag clear. This gives all slaves a shared timebase with no network. To make it true wall-clock, change **only** `masterNowMs()` to an SNTP/RTC epoch — the protocol and slaves are unaffected.

5. **Records fan out to all `LogsTarget` sinks.** The collector holds a list of `LogsTarget*` (`src/logs/LogsTarget.hpp`) and calls `write()` on each for every record, so multiple sinks (UART now, SD card later) run simultaneously. Add a sink by implementing `LogsTarget` and calling `collector.addTarget()` in `main.cpp` — no collector changes needed. **Critical contract:** `LogRecord::data` points into the shared SPI RX buffer and is valid *only during the `write()` call*; a sink that needs the bytes afterward (e.g. SD/FATFS) must copy them before returning. `LogRecord::seq` is the slave's monotonic record id — sinks dedup on it; `eventTimeMs`/`timeSynced` carry the slave's stamp, `collectedAtUs` the master poll time.

Other details worth knowing:
- SPI scratch buffers in `DataLogCollector::exchange` are `static DMA_ATTR` and shared across all pins — safe only because `scan()` runs on a single task. Keep collection single-threaded or give it per-context buffers.
- Pin choices in `main.cpp` are constrained by the C3: GPIO11–17 are SPI flash, 18/19 are USB, 20/21 are the UART0 console, 2/8/9 are strapping pins. Bus = MOSI 7 / MISO 1 / SCLK 6; CS = {10, 3, 4, 5}. Validate against these ranges before changing pins.
- `UartTarget` defaults to UART1/GPIO0 so it doesn't collide with the USB console on UART0; pass `UART_NUM_0` to reuse the console instead.
- Header-only `hardware/` peripheral wrappers (`SpiBus`, `SpiDevice`) follow the C++ style used across the other workshops (constructor stores config, `init()`/`attach()` does the ESP-IDF calls).
- **Two-phase exchange** (`exchange()`): a HAL SPI slave can't answer in the same frame it's still receiving, so every command is *two* 32-byte transactions — the command, then a `CMD_NOP` that clocks out the reply the slave prepared. `SLAVE_TURNAROUND_US` is the gap that lets the slave's ISR re-arm. Keep the master and slave on the same model.

## stm — build & SPI slave

Open `stm/miniproject-4/` in STM32CubeIDE and build the `Debug` configuration (device: STM32F401CCUX, linker `STM32F401CCUX_FLASH.ld`). Regenerate peripheral init from `miniproject-4.ioc` via the CubeMX view. HAL/CMSIS under `Drivers/` is generated — keep hand-written code inside the `/* USER CODE BEGIN/END */` guards so it survives regeneration. Firmware settings (I2C addresses, log intervals, layout) are centralized in `Inc/config.h`.

The SPI log-slave is **entirely in `Src/log_emission.c` + `Inc/log_emission.h`** (record ring buffer, protocol, and all SPI1 HAL glue — `MspInit`, `SPI1_IRQHandler`, `HAL_SPI_TxRxCpltCallback` are weak overrides defined there, *not* in `it.c`/`hal_msp.c`). `main.c` only `#include`s it, calls `LogEmission_Init()` once, and `LogEmission_AddText()` to queue records. SPI1 is a hardware-NSS slave on **PA4=NSS/CS, PA5=SCK, PA6=MISO, PA7=MOSI** (AF5, mode 0). It was added by hand (not via `.ioc`), and `HAL_SPI_MODULE_ENABLED` was turned on in `stm32f4xx_hal_conf.h` — re-running CubeMX won't recreate this, so don't expect the `.ioc` to own it. The protocol constants are duplicated (C port) at the top of `log_emission.c`; keep them in sync with `espc3/src/LogProtocol.hpp`. Wiring to the master: STM PA4/PA5/PA6/PA7 ↔ ESP CS/SCLK(6)/MISO(1)/MOSI(7), common ground.
