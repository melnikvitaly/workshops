#pragma once
#include "PWM.hpp"

// Hobby servo (SG90) driven through a PWM channel.
//
// An SG90 expects a 50 Hz signal (20 ms period). The pulse width selects the
// angle: ~0.5 ms -> one end, ~2.5 ms -> the other end, ~1.5 ms -> centre.
// The exact end-points vary per unit, so minUs/maxUs are configurable and
// should be trimmed to avoid the servo buzzing against its mechanical stop.
class Servo
{
    static constexpr uint32_t FREQ_HZ           = 50;     // SG90 control frequency
    static constexpr uint16_t DEFAULT_MIN_US    = 500;    // pulse at one end stop
    static constexpr uint16_t DEFAULT_MAX_US    = 2500;   // pulse at other end stop
    static constexpr float    DEFAULT_MIN_ANGLE = 0.0f;
    static constexpr float    DEFAULT_MAX_ANGLE = 180.0f;

    PWM&     _pwm;
    uint16_t _minUs;
    uint16_t _maxUs;
    float    _minAngle;
    float    _maxAngle;
    float    _angle;

public:
    // The PWM passed in should be created with a high resolution (e.g. 14-bit)
    // so the microsecond->duty conversion has enough steps across 0.5..2.5 ms.
    Servo(PWM& pwm,
          uint16_t minUs = DEFAULT_MIN_US, uint16_t maxUs = DEFAULT_MAX_US,
          float minAngle = DEFAULT_MIN_ANGLE, float maxAngle = DEFAULT_MAX_ANGLE)
        : _pwm(pwm), _minUs(minUs), _maxUs(maxUs),
          _minAngle(minAngle), _maxAngle(maxAngle),
          _angle((minAngle + maxAngle) / 2.0f) {}

    void init()
    {
        _pwm.init();
        _pwm.setFreq(FREQ_HZ);   // force 50 Hz regardless of how PWM was built
        write(_angle);           // park at centre
    }

    void write(float angle)
    {
        if (angle < _minAngle) angle = _minAngle;
        if (angle > _maxAngle) angle = _maxAngle;
        _angle = angle;

        float t = (angle - _minAngle) / (_maxAngle - _minAngle);     // 0..1
        uint32_t us = _minUs + (uint32_t)(t * (_maxUs - _minUs));
        _pwm.writeMicroseconds(us);
    }

    float angle() const { return _angle; }
};
