# Final Project
Laser on Gimbal that can automatically/manually be targeted at target (emulated or detected by Camera)

## Description of the project

## Base:
- take mini project from workshop-5-miniproject as base

### Nodes
-  PC:  Camera with detection of Current State and Setpoint of laser
  - take python scripts from workshop-5-miniproject
  - red dot and black dot detection is run on PC
    - encapsulate methods of recognitions in different files
      - (method1) OpenCV (like now in  workshop-5-miniproject)
      - (method2, DEFFER) - https://github.com/4ndr3aR/CSRT-tracker-standalone
  - SENDS ERROR (vector from red to black dot) to ESP32-S3 Node
    - implement different transports of sending data to ESP32-S3
      - MQTT server (hosted on PC)
      - UART (same port as LOGS)
    - use same JSON format for both transports
- ESP32-S3: Gimbal (2-axis) with Laser
  - take ESP IDF code from workshop-5-miniproject
  - calculates based on error and moves Gimbal (controls velocity of each axis)
  - additionally will render on I2C  0.96 display information about Gimbal state (current error, current PID gains, status of aiming)
    - also renders status of WIFI/BLE connections and list of clients
  - Also we will have the following methods of input
    - joystick
    - automatic (based on errors from PC)
    - manual from PC (create separate python script that will allow control by PC mouse)      
- ESP32-C3-mini: Wireless Joystick
  - Controls velocity manually by joystick position
  - Communicates with ESP32-S3 using several methods:      
  - (Optional) Mini display for some feedbacks on joystick
- STM32:  Telemetry Collector from ESP32-S3 write to several targets (flash-card, PC (over wireless protocol))

### Communications

- PC <==> ESP32-S3
  - UART, Duplex
- ESP32-S3 <==> ESP32-C3
  - NRF24L01+ wireless module 2.4 GHz (Maybe later Duplex with feedback on joystick)
  - BLE later (Optional as alternative method of communication)
- PC <===> joystick (no communication)
- ESP32-S3 ==> STM
  - SPI
- STM <==> PC
  - Wifi using MQTT  (when connected)
  - bluetooth  (optional)
- STM <==> fLASH memory
  - SPI 

### Features:

- ESP32-S3
  - Need to decide mode of sleep for ESP32-S3 until it receives signal from some source
  - different methods of inputs
    - "PC Mode" - allows to move manually from PC
    - "Joystick Mode"
    - "Automatic Mode" base on information from PC
- ESP32-C3
  - should be powered from battery and have deep sleep

## FINAL PROJECT VERIFICATION:

- requirements list (https://docs.google.com/spreadsheets/d/1Eip9bvWQhd6_9XutWGIf_u8ECNoNm-KQcv1kDBZhbcY/edit?gid=0#gid=0). Local copy (.\VERIFICATION\Requirements.html)
- PRESENTATION PRESENCE (Template https://docs.google.com/presentation/d/1kyIhydXwOALcR5MxjU38RSwIHLpqRsR5ne6F9L6wNbw/edit?slide=id.p1#slide=id.p1)
- VIDEO with DEMONSTRATION
  