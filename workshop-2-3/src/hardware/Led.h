#pragma once
#include <Arduino.h>
#include <hardware/Debug.h>
#include <hardware/PWM.h>

class LED
{
public:
  enum class LEDState : uint8_t
  {
    ON,
    OFF,
    BLINKING
  };

private:
  PWM &_pwm;
  LEDState _state = LEDState::OFF;
  unsigned long _blinkingTimeInState = 1000;
  unsigned long _blinkingLastChangeTime = 0;
  bool _blinkingLastState = false;
  Debug &dbg;

public:
  explicit LED(PWM &pwm, Debug &dbg) : _pwm(pwm), dbg(dbg) {}

  void init() { _pwm.init(); }

  void on()
  {
    _pwm.max();
    _state = LEDState::ON;
  }

  void power(uint8_t percents)
  {
    _pwm.power(percents);
    _state = LEDState::ON;
  }

  void blink(unsigned long timeInState)
  {
    _state = LEDState::BLINKING;
    _blinkingTimeInState = timeInState;
    _blinkingLastChangeTime = millis();
    _blinkingLastState = false;
  }

  void off()
  {
    _pwm.off();
    _state = LEDState::OFF;
  }

  void tick()
  {
    if (_state != LEDState::BLINKING)
      return;

    auto now = millis();
    if (now - _blinkingLastChangeTime > _blinkingTimeInState)
    {
      _blinkingLastChangeTime = now;
      _blinkingLastState = !_blinkingLastState;
      _blinkingLastState ? _pwm.on() : _pwm.off();
    }
  }

};
