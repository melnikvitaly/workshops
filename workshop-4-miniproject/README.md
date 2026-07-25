
# Faced problems

- failed wire caused wrong data over SPI

# Diagram

Star topology: the ESP32-C3 is the single SPI master / hub, every other node is a
slave on the shared bus with its own CS. Slaves never talk to each other.
Buses are drawn as nodes — everything hanging off one bus node shares the same
physical wires and is selected by address (I2C) or CS (SPI). The log stream is
one-way: slaves push records, the master only clocks the bus.

```mermaid
flowchart TB
    subgraph STM["STM32F401 — SPI1 slave, CS0"]
        direction TB
        LDR["Light sensor (LDR)"]
        ADCDMA["DMA2 Str0 — circular<br/>ADC sample ring"]
        SCPU["CPU<br/>average, format packets<br/>(TIME / LGHT)"]
        RING["TX ring buffer"]
        TXDMA["DMA2 Str3 — circular<br/>ring → SPI1 DR"]
        RXDMA["DMA2 Str2 — circular<br/>SPI1 DR → sink (debug drain)"]

        LDR -->|"ADC1 IN0, TIM2-paced"| ADCDMA
        ADCDMA -->|no CPU per sample| SCPU
        SCPU --> RING --> TXDMA
    end

    I2C1{{"I2C1 bus — SDA/SCL, 100 kHz<br/>CPU-driven, blocking HAL"}}
    RTC["DS1307 RTC · 0x68"]
    EEP["EEPROM archive · 0x50"]
    OLED["SSD1306 OLED · 0x3C"]

    SCPU <--> I2C1
    I2C1 <--> RTC
    I2C1 --> EEP
    I2C1 --> OLED

    SPIBUS{{"SPI bus — SCK / MOSI / MISO shared<br/>mode 0, 1 MHz, per-slave CS"}}
    TXDMA -->|"MISO: log packets"| SPIBUS
    SPIBUS -.->|"MOSI: 0xFF filler, ignored"| RXDMA

    subgraph S3["ESP32-S3 — SPI slave, CS1 (planned)"]
        LASER["Laser sensor"] --> S3CPU["CPU + DMA"]
    end
    S3CPU -.->|MISO| SPIBUS

    subgraph C3["ESP32-C3 — SPI2/FSPI master + collector"]
        direction TB
        MCS["CPU<br/>CS scan / hot-plug<br/>queue transfers"]
        MDMA["SPI2 DMA (auto ch)<br/>512-B blocks, ping-pong rx"]
        MCPU["CPU<br/>parse packets, seq / drop tracking"]
        MCS --> MDMA --> MCPU
    end
    SPIBUS -->|"clocked by master, CS selects slave"| MDMA

    I2C0{{"I2C0 bus — SDA 10 / SCL 9, 400 kHz<br/>CPU-driven"}}
    MCPU -->|"UART1 TX, GPIO0, 115200"| UARTLOG["Serial log sink"]
    MCPU --> I2C0 --> MOLED["SSD1306 OLED · 0x3C"]
    MCPU -->|"UART0"| CONSOLE["USB-serial console"]
```

| link | bus / port | who moves the bytes |
| --- | --- | --- |
| LDR → STM32 | ADC1 IN0, TIM2 trigger | DMA (circular), no CPU per sample |
| STM32 ↔ RTC / EEPROM / OLED | I2C1, one bus, 3 addresses | CPU, blocking HAL |
| STM32 → ESP32-C3 | SPI1 slave, hardware NSS, mode 0 | DMA both ends; CPU only fills / parses buffers |
| ESP32-C3 → sinks | UART1 (GPIO0), I2C0, USB console | CPU |
