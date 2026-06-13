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
- viewport/angle/max/min understanding and relation
- more granular movement within viewport/ Encoder precision toggle
- Pot endpoint calibration + deadzone
- ~~make center of view port as (0,0) for inputs~~ (done: ViewPort is centre+size; inputs work in centred coords and ViewPort.translate() places them)
- Commands for flash led is delayed as in the same queue as movement
- Encoder: stop increment out of the min/max/viewport
- "Command currentTarget()" does not look correct.
- use integers instead of float for coordinates   
- attach real joystick
- Try automate movement
- apply program filters
- Review/Rewrite Encoder







