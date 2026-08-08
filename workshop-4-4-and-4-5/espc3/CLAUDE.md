# espc3 — ESP32-C3 SPI slave (telemetry receiver)

ESP32-C3 firmware, the **SPI slave**. PlatformIO + ESP-IDF, C++. It receives one
32-byte telemetry frame per second from the STM32 master (see
`../stm/workshop-4-4/`), validates it, and prints it to the serial console.

Note the role: in `../../workshop-4-miniproject` this board was the SPI *master*
polling STM32 log-slaves. Here it only listens. Don't carry that project's
collector/hot-plug architecture over — none of it applies.

## Build / flash / monitor

PlatformIO CLI lives at `~/.platformio/penv/Scripts/pio.exe` (not on PATH). Run
from **inside `espc3/`**:

```sh
pio run                 # build (target: esp32-c3-devkitm-1)
pio run -t upload       # flash
pio device monitor      # USB console @ 115200 — where the frames print
pio run -t clean
```

There is no test suite. Verification is a successful `pio run` plus on-device
serial output.

## Architecture

Four files, and the whole program is a `while (true)` around one blocking call:

- `main.cpp` — pins, the loop, and the error/statistics reporting.
- `hardware/SpiSlave.hpp` — the `spi_slave` wrapper.
- `TelemetryPrinter.hpp` — unpacks a validated payload into named variables and
  formats the serial block. Swapping the presentation touches only this file.
- `hardware/StatusLed.hpp` — non-blocking activity blink (unchanged from the
  mini-project).
- `../protocol/telemetry_packet.h` — the wire format, on the include path via
  `INCLUDE_DIRS` in `src/CMakeLists.txt`. It is the *same file* the STM32
  compiles, not a copy; see the top-level `CLAUDE.md`.

Key design points — preserve them when editing:

1. **`SPI_DMA_CH_AUTO` is required — do not "restore" `SPI_DMA_DISABLED`.**
   This was tried, and on the C3 it yields exactly one good frame (the first
   after boot) followed by permanent garbage: non-byte-aligned fragments of 3,
   41, 6, 21 bits. The cause is in the driver. `spi_slave_queue_trans()` does
   not arm the hardware — it queues and calls `esp_intr_enable()`; arming
   happens only inside `spi_intr`, whose re-arm path branches on DMA and, in
   the no-DMA branch, never resets the RX FIFO
   (`s_spi_slave_prepare_data`: `fifo_reset(tx=true, rx=false)`) and skips
   `restore_cs()`. The first transaction escapes this because
   `spi_slave_initialize()` armed it on freshly reset hardware.

   This does **not** violate the workshop's "no circular DMA transferring or
   async transmissions": nothing is circular or free-running, one transaction
   exists at a time, `receive()` blocks until it completes, and one frame is
   still one CS assertion. DMA is only how the driver moves 32 bytes during a
   call we are already blocked inside. The STM32 master uses no DMA at all.

   Consequence: the rx buffer must be `DMA_ATTR` (internal RAM, word-aligned)
   and the length a whole number of bytes — `spi_slave_queue_trans()` rejects
   anything else outright.

2. **Arming, not polling.** An SPI slave receives into a buffer the driver was
   handed *in advance*; a frame that arrives while nothing is armed is not
   captured at all and leaves no trace but a gap in the sequence numbers.
   `SpiSlave::receive()` is one `spi_slave_transmit()` with **`portMAX_DELAY`**.
   The infinite timeout is the point: `spi_slave_transmit()` is internally
   `queue_trans` + `get_trans_result` (with a `//ToDo: check if any spi
   transfers in flight` above it), so on a *finite* timeout the queued
   transaction stays armed in hardware and the next call stacks another one
   behind it. Blocking forever keeps it to exactly one. The "link quiet" notice
   therefore lives on an `esp_timer` in `main.cpp`, not on this path.

3. **One transaction in flight, ever** (`queue_size = 1`). One CS assertion is
   one frame; there is nothing to pipeline.

4. **Validate before trusting.** `Telemetry_Parse` checks magic, version,
   length and CRC, and copies into the output only on success — a rejected
   frame can never leave half-parsed values behind. `trans_len` is checked
   against the expected size separately, because a short transaction is a
   clock/CS problem rather than corruption and deserves a different message.

Other details worth knowing:

- Pin choices are constrained by the C3: GPIO11–17 are the SPI flash, 18/19 are
  USB, 20/21 are the UART0 console, 2/8/9 are strapping pins. Bus = MOSI 6 /
  MISO 5 / SCLK 4, CS = 7 — the same wiring as the mini-project.
- The receive buffer is `static WORD_ALIGNED_ATTR`: the driver writes into it
  while `receive()` is not on the stack, and the peripheral moves whole words.
- `printf` goes to the USB console and is used for the per-frame block;
  `ESP_LOGW`/`ESP_LOGE` are used only for problems, so the block stays clean.
- No floats anywhere: the C3 has no FPU, and scaled integers print exactly.
