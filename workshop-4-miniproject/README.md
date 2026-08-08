# Workshop 4 mini-project
- NOTE: I make it to work somehow (but with a lot of iterations with the CLAUDE)

![image](./images/image.jpg)

## Protocol definitions
- Wire structs: [espc3/src/LogPacket.hpp](espc3/src/LogPacket.hpp#L47-L70) — `DateTime`, `Header`, `ValueType`.
- Packet read/parsing: [espc3/src/LogProtocol.hpp](espc3/src/LogProtocol.hpp#L79-L105) — `parsePacket()`.
- Packet build on STM32: [stm/miniproject-4/Src/log_emission.c](stm/miniproject-4/Src/log_emission.c#L54-L71) — `build_packet()`.

# Faced problems


- faced failed wire caused wrong data over SPI
- faced SD card mount on the shared SPI bus failed intermittently (timeout / invalid response / CRC errors) whenever the STM32 slave was powered — its SPI1 uses hardware NSS (CS7), and until the ESP32 collector's first `attach()` that CS pin was left floating, so noise on it could make the STM drive MISO at the wrong time and corrupt the SD card's transactions. Fixed by driving all CS pins high immediately after bus init, before anything else touches the bus; a hardware pull-up on CS/NSS lines is the more complete fix since it also covers the pre-firmware boot window.
- faced DMA circular streaming from STM32 to ESP-C3 via SPI (just to try) causes packets to be split in the middle on ESP32 side 
- for some reason display connected to STM fails to communicate periodically (Errors on screen and reinitialization in the code) 
- faced the record flow stalling while a **bus scan** runs — real on both ends:
  - STM32 I2C scan ([`poll_i2c_scan`](stm/miniproject-4/Src/display_ui.c#L64-L71), every 10 s) pings 126 addresses blocking, 5 ms each — up to ~600 ms with no main loop, so no *new* records enter the ring (the SPI link itself keeps running on circular DMA; the master just re-reads old packets). *Fix: a few addresses per loop pass, or ping only the 3 addresses actually used.*
  - ESP32 hot-plug probe ([`probeAbsent`](espc3/src/main.cpp#L221-L225), every 1 s) pauses draining for a full 512-byte block per absent CS ≈ 6 ms — about one ring lap. *Fix: one pin per period round-robin + a short (~32 B) probe block.*


# Diagram

Star topology: the ESP32-C3 is the single SPI master / hub, every other node is a
slave on the shared bus with its own CS. Slaves never talk to each other.
Buses are drawn as thick lines — everything touching one shares the same
physical wires and is selected by address (I2C) or CS (SPI). The log stream is
one-way: slaves push records, the master only clocks the bus.

![System block diagram](kicad/miniproject-4-blocks.svg)

Generated, not hand-drawn — see [`kicad/`](kicad/) for the generator and for the
pin-level schematic (`miniproject-4.kicad_sch`) that this block view summarises.

| link | bus / port | who moves the bytes | DMA mode |
| --- | --- | --- | --- |
| LDR → STM32 | ADC1 IN0, TIM2 trigger | DMA, no CPU per sample | DMA2 Stream0/Ch0, **circular**, halfword, half+full IRQ → `g_adc[20]` ([msp](stm/miniproject-4/Src/stm32f4xx_hal_msp.c#L108-L125), [start](stm/miniproject-4/Src/sensor_stream.c#L43)) |
| STM32 ↔ RTC / EEPROM / OLED | I2C1, one bus, 3 addresses | CPU, blocking HAL | none |
| STM32 → ESP32-C3 | SPI1 slave, hardware NSS, mode 0 | DMA both ends; CPU only fills / parses buffers | **STM TX:** DMA2 Stream3/Ch3, **circular**, byte, free-running over the 2 KB ring, TC IRQ only counts laps ([msp](stm/miniproject-4/Src/stm32f4xx_hal_msp.c#L264-L281), [start](stm/miniproject-4/Src/log_emission.c#L511-L525))<br>**STM RX (debug):** DMA2 Stream2/Ch3, **circular**, polled, `LOGEMIT_DEBUG_SPI` only ([init](stm/miniproject-4/Src/log_emission.c#L291-L314))<br>**ESP:** GDMA via `spi_master`, **queued transactions** (no circular mode) — `SPI_DMA_CH_AUTO` ([bus](espc3/src/hardware/SpiBus.hpp#L22-L34)), `queue_trans`/`get_trans_result` ([device](espc3/src/hardware/SpiDevice.hpp#L76-L96)), `DMA_ATTR` tx + ping-pong rx ([buffers](espc3/src/DataLogCollector.hpp#L297-L298)) |
| ESP32-C3 → sinks | UART1 (GPIO0), I2C0, USB console | CPU | none |
| ESP32-C3 ↔ microSD | same SPI2/FSPI bus, dedicated CS3 | synchronous SDSPI + FATFS | rides the same DMA-enabled bus, but blocking calls — no queuing ([mount](espc3/src/logs/SdCardTarget.hpp#L38-L52)) |

The asymmetry — circular stream on one end, fixed-size queued blocks on the
other — is why packets arrive split (see Faced problems).
