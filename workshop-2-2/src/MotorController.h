#pragma once
#include <Arduino.h>
#include <Config.h>
#include <hardware/PWM.h>
#include <hardware/Debug.h>
#include <hardware/ADC.h>
#include <DelayMeasure.h>

class MotorController
{
    enum class State : uint8_t { MotorsOn, MotorsOff, Testing };

    PWM &_motors;
    ADC &_adc;
    DelayMeasure &_m1;
    DelayMeasure &_m2;
    Debug &_dbg;
    State _state = State::MotorsOn;
    uint32_t _stateAt = 0;
    ulong _lastTestAt = 0;
    bool _testMeasuring = false;

    void transition(State next)
    {
        _state = next;
        _stateAt = millis();
    }

public:
    MotorController(PWM &motors, ADC &adc, DelayMeasure &m1, DelayMeasure &m2, Debug &dbg)
        : _motors(motors), _adc(adc), _m1(m1), _m2(m2), _dbg(dbg) {}

    void begin()
    {
        _motors.on();
        _lastTestAt = millis();
        transition(State::MotorsOn);
    }

    void tick()
    {
        uint32_t now = millis();

        if (_state != State::Testing)
            _motors.power(_adc.percent());

        if (_state != State::Testing && now - _lastTestAt >= Config::RUN_TEST_EVERY_MS)
        {
            _dbg.print("running test");
            _motors.off();
            _testMeasuring = false;
            transition(State::Testing);
            return;
        }

        switch (_state)
        {
        case State::MotorsOn:
            if (now - _stateAt >= Config::MOTORS_ON_MS)
            {
                _motors.off();
                transition(State::MotorsOff);
            }
            break;
        case State::MotorsOff:
            if (now - _stateAt >= Config::MOTORS_OFF_MS)
            {
                _motors.on();
                transition(State::MotorsOn);
            }
            break;
        case State::Testing:
            if (!_testMeasuring)
            {
                if (now - _stateAt >= 100)
                {
                    _m1.checkBeforeStart();
                    _m2.checkBeforeStart();
                    _motors.power(100);
                    _motors.on();
                    _m1.onStart();
                    _m2.onStart();
                    _testMeasuring = true;
                }
            }
            else
            {
                _m1.tick();
                _m2.tick();
                if (_m1.isCompleted() && _m2.isCompleted())
                {
                    _dbg.print("test finished");
                    _lastTestAt = millis();
                    transition(State::MotorsOn);
                }
            }
            break;
        }
    }
};
