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

## How it works

Each `scan()` pass, for every CS pin the collector:

1. **attaches** an SPI device on that CS,
2. **probes** it (`CMD_PING`) — a device is "present" only if it returns a valid
   framed response,
3. if present, **drains** its log queue (`CMD_GET_LOG`) until the slave reports
   `ST_NO_DATA`,
4. **detaches**.

Only one device is on the bus at a time, so the number of scanned pins is not
limited by the SPI host's concurrent-device cap. Add/remove transitions are
logged and per-pin record counts are kept.

## Protocol (`src/LogProtocol.hpp`)

Fixed **32-byte frames**, both directions, little-endian, XOR checksum in the
last byte (on requests *and* responses):

```
request   [0] cmd  [1..4] arg(u32)  [5..30] reserved  [31] checksum
              arg = lastReceivedId (CMD_GET_LOG) | masterTimeMs (CMD_SET_TIME)
response  [0] MAGIC A5  [1] status  [2] len  [3..6] recordId(u32)
          [7..10] recordTimeMs(u32)  [11..12] dropped(u16)  [13..30] payload(18)  [31] checksum
```

SPI gives the master no ACK-style presence signal, so presence is inferred: a
device is "present" only if its reply passes `MAGIC` + checksum. An empty CS line
(MISO pulled high) reads as `0xFF` and can never satisfy both.

**Reliable delivery — cumulative ack by record id.** Every record has a
monotonic `recordId`. A `CMD_GET_LOG` request carries `lastReceivedId`; the slave
marks everything `<=` it as processed and returns the oldest record with a
greater id (or `ST_NO_DATA`). The master advances its high-water mark **only
after** a frame validates and is delivered, so a corrupted or lost response is
just re-requested at the same offset next pass — no record is lost and processed
records are never resent. The request checksum lets the slave reject a garbled
offset before acting on it.

**Loss reporting.** The slave holds records until acked, so it only ever loses
data by discarding the oldest when its buffer overflows (master absent/too slow
for too long). It counts every such drop in the monotonic `dropped` field, sent
in *every* response. The master diffs it per device and logs
`slave dropped N records (buffer overflow)` when it grows — so loss is visible,
not silent. (The authoritative log still lives in the slave's EEPROM; the SPI
ring is only a delivery buffer.)

**Time — master-as-reference.** The master is the time authority: it pushes its
own clock (ms since boot) to each slave via `CMD_SET_TIME` on plug-in and every
`TIME_SYNC_MS`. The slave stamps each record's `recordTimeMs` on that timebase
and sets `FLAG_TIME_SYNCED` (bit 7 of `status`) once synced; before that it
stamps raw uptime with the flag clear, so a sink knows whether the time is
master-referenced or not. All slaves thus share one timebase with no network;
swapping the master's `masterNowMs()` for an SNTP/RTC epoch later makes it true
wall-clock with **no protocol change**.

**Two-phase exchange.** A HAL SPI slave is still clocking a command in when it
must already be shifting MISO out, so it can't answer in the same frame. Each
command is therefore *two* 32-byte transactions: the command, then a `CMD_NOP`
(0x00) that clocks out the reply the slave prepared. `exchange()` handles this;
`SLAVE_TURNAROUND_US` is the gap that lets the slave's ISR re-arm.

Requests: `CMD_NOP` (0x00), `CMD_PING` (0x01), `CMD_GET_LOG` (0x02),
`CMD_SET_TIME` (0x03). Statuses: `ST_OK`, `ST_NO_DATA`. A reference slave lives
in `stm/miniproject-4/Src/log_emission.c` (STM32F401, SPI1 on PA4–PA7). See the
contract block at the top of `LogProtocol.hpp`.

## Log sinks (`LogsTarget`)

Collected records are not printed directly — they are handed to `LogsTarget`
sinks. The collector holds a list of targets and **fans every record out to all
of them**, so multiple sinks run at the same time:

```cpp
static UartTarget uartTarget(UART_NUM_1, GPIO_NUM_0, 115200);
// static SdCardTarget sdTarget(...);   // later

collector.addTarget(&uartTarget);
// collector.addTarget(&sdTarget);      // both receive every record
```

`LogsTarget` is a small interface — `init()`, `write(const LogRecord&)`,
`name()`. `UartTarget` streams each record as a text line out of a UART
(defaults to UART1/GPIO0 so it does not collide with the USB console; watch it
with a USB-serial adapter, or pass `UART_NUM_0` to reuse the console). To add
the SD-card sink later, implement `LogsTarget` in `src/logs/SdCardTarget.hpp`
and register it — no collector changes needed.

## Source layout

| File | Role |
| ---- | ---- |
| `src/main.cpp` | pins, bus/collector wiring, sink registration, scan loop |
| `src/DataLogCollector.hpp` | scan / probe / drain, presence tracking, sink fan-out |
| `src/LogProtocol.hpp` | wire frame + validation |
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
registered sinks. Console:

```
I (300) COLLECTOR: target 'uart' added (1 total)
I (312) LOG_MASTER: SPI data-log collector up. Scanning 4 CS pins every 1000 ms.
I (1320) COLLECTOR: Device ADDED on CS10
I (5340) COLLECTOR: Device REMOVED on CS10
```

UART sink line (UART1/GPIO0), one per record — `t=` is master-timebase ms,
`up=` is unsynced slave uptime:

```
[t=1200] CS10 #1: temp=23.4C
```
