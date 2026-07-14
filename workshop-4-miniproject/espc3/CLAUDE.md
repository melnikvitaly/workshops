# espc3 — ESP32-C3 SPI master (data-log collector)

ESP32-C3 firmware, the **SPI master**. PlatformIO + ESP-IDF, C++. This is the
active development target. It collects log records streamed by one or more STM32
SPI slaves (see `../stm/miniproject-4/`) and fans them out to log sinks.

## Build / flash / monitor

PlatformIO CLI lives at `~/.platformio/penv/Scripts/pio.exe` (not on PATH). Run
from **inside `espc3/`**:

```sh
pio run                 # build (target: esp32-c3-devkitm-1)
pio run -t upload       # flash
pio device monitor      # console UART0 @ 115200
pio run -t clean
```

There is no test suite. Verification is a successful `pio run` plus on-device
serial output.

## Architecture

An SPI **bus scanner / hot-plug collector**, not a fixed point-to-point link.
`app_main` (`src/main.cpp`) sets up the bus and one or more log sinks, then loops
`collector.scan()` every `SCAN_PERIOD_MS`. Key design points — preserve them when
editing:

1. **One device on the bus at a time** (`DataLogCollector::scan`). Each pass, for
   every predefined CS pin, the collector attaches an SPI device, reads a block of
   its log stream, then detaches. Because only one device is ever attached at
   once, the number of scanned CS pins is *not* limited by the SPI host's
   concurrent-device cap. Don't "optimize" this into keeping devices attached
   without re-checking that cap.

2. **One-way, best-effort streaming.** Slaves don't answer requests — the master
   only ever clocks dummy bytes and each slave continuously presents self-framed
   log packets that the master parses out of the block it read. There is no ack,
   no time sync, and no command set. A record the master fails to collect before
   the slave's buffer overwrites it is simply lost; the slave reports how much it
   dropped so the master can flag it. The wire format, framing, and the
   master/slave contract live in `src/LogProtocol.hpp` — treat that header as the
   source of truth and keep the STM slave's mirror of it in sync.

3. **Records fan out to all `LogsTarget` sinks.** The collector holds a list of
   `LogsTarget*` (`src/logs/LogsTarget.hpp`) and calls `write()` on each for every
   record, so multiple sinks (UART now, SD card later) run simultaneously. Add a
   sink by implementing `LogsTarget` and calling `collector.addTarget()` in
   `main.cpp` — no collector changes needed. **Critical contract:**
   `LogRecord::data` points into a shared buffer valid *only during the `write()`
   call*; a sink that needs the bytes afterward (e.g. SD/FATFS) must copy them
   before returning.

Other details worth knowing:
- The collector's per-device parse buffers and the shared SPI scratch buffers are
  sized for single-task use (`scan()` runs on one task). Keep collection
  single-threaded or give it per-context buffers.
- Pin choices in `main.cpp` are constrained by the C3: GPIO11–17 are SPI flash,
  18/19 are USB, 20/21 are the UART0 console, 2/8/9 are strapping pins. Bus =
  MOSI 7 / MISO 1 / SCLK 6; CS = {10, 3, 4, 5}. Validate against these ranges
  before changing pins.
- `UartTarget` defaults to UART1/GPIO0 so it doesn't collide with the USB console
  on UART0; pass `UART_NUM_0` to reuse the console instead.
- Header-only `hardware/` peripheral wrappers (`SpiBus`, `SpiDevice`) follow the
  C++ style used across the other workshops (constructor stores config,
  `init()`/`attach()` does the ESP-IDF calls).
