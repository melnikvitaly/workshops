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


## Plan:
1. Read values from potentiometer and encoder and contevt to desired (x,y) accroding to coordinates from STep2
2. Assume that (0,0) is center - both servo are at the middle value. Areas is:
    - Left-buttom: (-Xm,-Ym)
    - Right-up: (-Xm,-Ym)
    - So width is 2*Xm and height 2*Ym
3. Convert current (x,y) into desired angles for each servo
4. Convert each angle for each servo 

### Extra:
 - apply program filters
 - Try automate movement
 - click on encoder flashes laser for 1s
   - using relay and LED
   - look how ckick is handled, do we need to chaged it? 
   - 
### Known issues/todo LATER
- switching of relay during rest/boot/flash
- viewport/angle/max/min understanding and relation
- more granular movement within viewport/  Encoder precision toggle
- Pot endpoint calibration + deadzone
- make center of view port as (0,0) for inputs
- attach real joystick
- input should also provide command. not only (x,y)
- Encoder: stop increment out of the min/max/viewport

## Design:
- (same approaches like in previous workshops (see "../workshop-3-3"))
  - supper
- create class for Encoder (create new)
- create class for Potentiometer (search for ADC.hpp)
- create PWM class
- create Servo class that will use PWN
  - two instances servoceA (servoce below) and servoB (servoce above)
- create class GimbalController
- create class GimbalInput that uses other inputs and outputs calculated (x,y) based on current values from inputs


## Notes
- comment code for interaction with encoder in order to understand better how to work with encode 
- make gimbal GimbalController independent from what is used for controlling (so it accepts expected (x,y))





