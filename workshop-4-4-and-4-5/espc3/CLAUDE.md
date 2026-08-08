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

1. **`SPI_DMA_DISABLED`, deliberately.** The workshop rules out DMA on this
   link. Without it the driver works out of the hardware FIFO, which caps a
   transaction at `SOC_SPI_MAXIMUM_BUFFER_SIZE` (64 bytes on the C3). The frame
   is 32, so the cap costs nothing and the data path loses its descriptors,
   cache-alignment rules and 4-byte length rounding. If you ever grow the frame
   past 64 bytes this is the constraint that bites first.

2. **Arming, not polling.** An SPI slave receives into a buffer the driver was
   handed *in advance*; a frame that arrives while nothing is armed is not
   captured at all and leaves no trace but a gap in the sequence numbers.
   `SpiSlave::receive()` queues one transaction and then waits for it, and on
   timeout leaves it armed so the caller comes back onto the same one. Do not
   replace it with `spi_slave_transmit()` in a loop with a finite timeout —
   that re-queues on every call and stacks transactions behind the first for as
   long as the link is quiet.

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
