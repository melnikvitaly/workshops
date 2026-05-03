#pragma once
#include <Arduino.h>
#include <hardware/Debug.h>

class DelayMeasure
{
  ulong _tick;
  uint8_t _pin;
  int _awaitingValue;
  Debug &dbg;

public:
  DelayMeasure(uint8_t pin, int awaitingValue, Debug &dbg) : _pin(pin), _awaitingValue(awaitingValue), dbg(dbg)
  {
  }

  void init()
  {
    pinMode(_pin, _awaitingValue == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  }

  bool isCompleted()
  {
    return _tick == 0;
  }

  void checkBeforeStart()
  {
    if (digitalRead(_pin) == _awaitingValue)
    {
      dbg.print("ERR: not expected value before start");
    }
  }

  void onStart()
  {
    _tick = millis();
  }

  void tick()
  {
    if (_tick > 0)
    {
      if (digitalRead(_pin) == _awaitingValue)
      {
        auto elapsed = millis() - _tick;
        dbg.print("Measured (ms) " + String(elapsed));
        _tick = 0;
      }
    }
  }
};
