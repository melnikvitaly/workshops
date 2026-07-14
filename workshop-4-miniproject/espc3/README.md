# Workshop 4 — Mini-project: SPI data-log collector

ESP-IDF (PlatformIO) firmware for an **SPI master** that collects *data logs*
from slave devices. Instead of talking to one fixed slave, it **scans a
predefined list of CS pins** and hot-plugs devices in and out: attach a slave to
any listed CS and it is picked up on the next pass; unplug it and it is dropped.

Derived from `workshop-4-4/src/main.cpp` (single-CS SPI master), refactored into
the header-only `hardware/` C++ style used across the other workshops.

## Hardware

Target: **ESP32-C3 DevKitM** (`esp32-c3-devkitm-1`). On the C3, GPIO11-17 are the
SPI flash, 18/19 are USB and 20/21 the UART0 console, so the bus/CS pins are
chosen from the remaining free low GPIOs (routed to FSPI via the GPIO matrix).

| Signal | GPIO |
| ------ | ---- |
| MOSI   | 7    |
| MISO   | 1    |
| SCLK   | 6    |
| CS (scanned) | 10, 3, 4, 5 |

SPI2_HOST (FSPI), mode 0, 1 MHz. Edit `CS_PINS[]` in `src/main.cpp` to change
the scanned set (keep off the C3 strapping pins 2/8/9 and the reserved ranges
above). Up to `DataLogCollector::MAX_DEVICES` (8) pins are supported.

The reference slave is an STM32F401 (SPI1 on PA4–PA7); see `../stm/miniproject-4/`.

## How it works

Each `scan()` pass, for every CS pin, the collector attaches an SPI device, reads
a block of the log stream that slave is emitting, parses records out of it, and
detaches. Only one device is on the bus at a time, so the number of scanned pins
is not limited by the SPI host's concurrent-device cap. It is a one-way,
best-effort stream — the master only clocks; the slave never answers a request.

For the details — wire format, framing, presence detection, loss reporting, and
the master/slave contract — read the code, which is the source of truth:

- `src/LogProtocol.hpp` — the packet format and the full protocol contract.
- `src/DataLogCollector.hpp` — scan, stream parsing, presence tracking, sink fan-out.

## Log sinks (`LogsTarget`)

Collected records are not printed directly — they are handed to `LogsTarget`
sinks, and the collector **fans every record out to all registered sinks**, so
multiple sinks run at the same time:

```cpp
static UartTarget uartTarget(UART_NUM_1, GPIO_NUM_0, 115200);
// static SdCardTarget sdTarget(...);   // later

collector.addTarget(&uartTarget);
// collector.addTarget(&sdTarget);      // both receive every record
```

`LogsTarget` is a small interface — `init()`, `write(const LogRecord&)`,
`name()` — in `src/logs/LogsTarget.hpp`. `UartTarget` streams each record as a
text line out of a UART (defaults to UART1/GPIO0 so it does not collide with the
USB console). Add the SD-card sink later by implementing `LogsTarget` in
`src/logs/SdCardTarget.hpp` and registering it — no collector changes needed.

## Source layout

| File | Role |
| ---- | ---- |
| `src/main.cpp` | pins, bus/collector wiring, sink registration, scan loop |
| `src/DataLogCollector.hpp` | scan / stream parse, presence tracking, sink fan-out |
| `src/LogProtocol.hpp` | wire packet format + parsing + protocol contract |
| `src/hardware/SpiBus.hpp` | SPI host init |
| `src/hardware/SpiDevice.hpp` | per-CS attach / detach / transfer |
| `src/logs/LogsTarget.hpp` | `LogRecord` + abstract sink interface |
| `src/logs/UartTarget.hpp` | UART sink (implements `LogsTarget`) |

## Build / flash / monitor

```sh
pio run                 # build
pio run -t upload       # flash
pio device monitor      # serial log (115200)
```

Presence/diagnostics go to the console (UART0); the records themselves go to the
registered sinks.
