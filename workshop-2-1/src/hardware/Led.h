#pragma once
#include <Arduino.h>
#include <hardware/Debug.h>

class LED
{
public:
  enum class LEDState : uint8_t
  {
    ON,
    OFF,
    BLINKING
  };
  enum class LEDPin : uint8_t
  {
  };

private:
  LEDPin _pin;
  LEDState _state = LEDState::OFF;
  unsigned long _blinkingTimeInState = 1000;
  unsigned long _blinkingLastChangeTime = ULONG_MAX;
  unsigned long _blinkingLastState = LOW;
  uint8_t pinVal() const { return static_cast<uint8_t>(_pin); }
  Debug &dbg;

public:
  explicit LED(LEDPin pin, Debug &dbg) : _pin(pin), dbg(dbg) {}
  LEDState state() { return _state; }
  void init()
  {
    pinMode(pinVal(), OUTPUT);
    digitalWrite(pinVal(), LOW);
  }
  void on()
  {
    digitalWrite(pinVal(), HIGH);
    _state = LEDState::ON;
  }

  void blink(unsigned long timeInState)
  {
    _state = LEDState::BLINKING;
    _blinkingTimeInState = timeInState;
  }
  void off()
  {
    digitalWrite(pinVal(), LOW);
    _state = LEDState::OFF;
  }

  void tick()
  {
    if (_state != LEDState::BLINKING)
    {
      return;
    }

    auto now = millis();
    if (now - _blinkingLastChangeTime > _blinkingTimeInState)
    {
      _blinkingLastChangeTime = now;
      _blinkingLastState = _blinkingLastState ^ 1;
      digitalWrite(pinVal(), _blinkingLastState);
    }
  }
  void set(LEDState state, unsigned long blinkingTime = 0)
  {
    if (state == _state)
    {
      return;
    }
    dbg.dumpChange("state change", _state, state);
    _state = state;
    switch (_state)
    {
    case LEDState::BLINKING:
      blink(blinkingTime);
      break;
    case LEDState::OFF:
      off();
      break;
    case LEDState::ON:
      on();
      break;
    }
  }
  bool isUp() const { return _state == LEDState::ON; }
  void toggle() { _state == LEDState::ON ? off() : on(); }
};
