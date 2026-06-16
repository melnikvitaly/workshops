# Mini Project 3

## Task
LED laser on gimbal (2 axises) that are controlled by potentiometer and encoder
## Parts
2 servo SG90 for [gimbal](https://www.printables.com/model/1042622-cheap-fpv-gimbal-pantilt)
1 [LED laser](https://uamper.com/%D0%9B%D0%B0%D0%B7%D0%B5%D1%80-5%D0%BC%D0%92%D1%82-650%D0%BD%D0%BC-%D1%82%D0%BE%D1%87%D0%BA%D0%B0)
    - connected via relay
1 - potentiometer
1 - encoder 
power for servoce is from external source

ESP-S3 DevKit

  
### Known issues/todo LATER
- switching of relay during rest/boot/flash
  - control power of relay separately
  - understand exactly behavior of the PINs
- preserve angles across reboots
  - save to NVS (Non-Volatile Storage) 
- Review/Rewrite Encoder ~~debounce on encoder/ PCNT~~ - src\drivers\EncoderPcnt.hpp
- more granular movement within viewport/Encoder precision toggle
- Pot/Encoder calibration - understand that movement is linearly changed with rotation
- deadzone for Encoder - when counter is large and we try rotate back then nothing changed
- ~~make center of view port as (0,0) for inputs~~ (done: ViewPort is centre+size; inputs work in centred coords and ViewPort.translate() places them)
- Commands for flash led is delayed as in the same queue as movement
- ~~Encoder: stop increment out of the min/max/viewport~~
- ~~"Command currentTarget()" does not look correct.~~ - replaced by update() calls that prepare commands
- use integers instead of float for coordinates   (will not do)
- attach real joystick (later)
- ~~Try automate movement~~ AutoInput only for now
- ~~apply program filters~~ - sma/ema were tried








