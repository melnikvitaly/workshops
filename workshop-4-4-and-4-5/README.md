Module 4.4 and 4-5. SPI: Fast data transfer for displays and memory cards (Master/Slave)

## Tasks
1. ON STM32
   1. Read from the BME280 (In our case it will be I2C): temperature (°C), humidity (%RH), pressure (hPa).
   2. Add three new fields to the existing OLED screen output (previous homework): T, RH, P (for example: T: 23.4°C RH: 45% P: 1013 hPa).
   3. Refresh the BME280 data at least once per second (a separate timer/timestamp is fine), without delay().
2. On ESP-C3
   1. Develop your own custom SPI data transfer protocol that transmits date, time, temperature, light level percentage, humidity and pressure.
   2. On the ESP32, implement reception of this data. Parse it into separate variables and print it in a nice format to the serial monitor.
   3. Output received data to serial port
   4. No SD card is connected to ESP-C3 - received output just only to Serial
3. MAKE ESP-C3 to be SLAVE and STM to be MASTER
   1. Use only synchronous communication (no circular DMA transferring or async transmissions) between ESP and STM
4. Wiring: reuse the wiring from ../workshop-4-miniproject (take that project as the starting point)
   1. Additionally, a BME280 will be added to the Tiny RTC and OLED on the same I2C bus
