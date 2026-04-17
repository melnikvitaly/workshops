#pragma once
#include <Arduino.h>

class L298NMotor
{

public:
    enum class DIRECTION
    {
        STOP = 0,
        FORWARD = 1,
        BACKWARD = -1,
    };
    constexpr static uint8_t MAX_SPEED = 100;
private:
    std::array<uint8_t, 3> _pins; //EN, IN1, IN2
    uint8_t dutyCycle = 255;
    DebugOut &dbg;

    DIRECTION direction = DIRECTION::STOP;

public:
    L298NMotor(std::array<uint8_t, 3> pins, DebugOut &dbg) : _pins(pins),
                                                             dbg(dbg)
    {
    }

    void init()
    {
        pinMode(_pins[0], OUTPUT);
        pinMode(_pins[1], OUTPUT);
        pinMode(_pins[2], OUTPUT);
    }

    void move(DIRECTION newDirection, uint8_t newSpeed = -1)
    {

        if (newSpeed > -1)
        {
            speed(newSpeed);
        }
        direction = newDirection;
        writeToPins();
    }

    void forward(uint8_t newSpeed = -1)
    {

        if (newSpeed > -1)
        {
            speed(newSpeed);
        }
        direction = DIRECTION::FORWARD;
        writeToPins();
    }

    void backward(uint8_t newSpeed = -1)
    {
        if (newSpeed > -1)
        {
            speed(newSpeed);
        }
        direction = DIRECTION::BACKWARD;
        writeToPins();
    }

    void stop()
    {
        speed(0);
        direction = DIRECTION::STOP;
        writeToPins();
    }

    void maxSpeed()
    {
        speed(MAX_SPEED);
    }

    void speed(uint8_t valuePercents)
    {

        auto oldValue = dutyCycle;
        dutyCycle = map(valuePercents, 0, MAX_SPEED, 0, 255);

        if (oldValue != dutyCycle)
        {
            dbg.print(String("speed(") + valuePercents + "%," + dutyCycle + ")");
            writeToPins();
        }
    }

    void writeToPins()
    {
        if (direction == DIRECTION::FORWARD)
        {
            dbg.print(String("forward(") + dutyCycle + ")");
            analogWrite(_pins[0], dutyCycle);
            digitalWrite(_pins[1], HIGH);
            digitalWrite(_pins[2], LOW);
        }
        else if (direction == DIRECTION::BACKWARD)
        {
            dbg.print(String("backward(") + dutyCycle + ")");
            
            analogWrite(_pins[0], dutyCycle);            
            digitalWrite(_pins[1], LOW);
            digitalWrite(_pins[2], HIGH);
            
        }
        else
        {
            dbg.print(String("stop(") + dutyCycle + ")");
            digitalWrite(_pins[0], LOW);
            digitalWrite(_pins[1], LOW);
            digitalWrite(_pins[2], LOW);
        }
    }
};