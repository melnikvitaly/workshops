#pragma once
#include <Arduino.h>

class LEDs
{
  static constexpr size_t RED_INDEX    = 0;
  static constexpr size_t YELLOW_INDEX = 1;
  static constexpr size_t GREEN_INDEX  = 2;

private:
  uint8_t *pins;  // non-owning pointer — caller must keep the array alive
  uint8_t *state; // owning — tracks HIGH/LOW per pin index
  size_t count;
  void setPin(size_t index, uint8_t level);
  size_t findIndex(uint16_t pin) const;

public:
  /// @brief
  /// @param pins RED, YELLOW, GREEN, other
  /// @param count number of passed pins
  LEDs(uint8_t *pins, size_t count);
  ~LEDs();
  void init();
  void upAll();
  void downAll();
  void upRed();
  void downRed();
  void upYellow();
  void downYellow();
  void upGreen();
  void downGreen();
  void up(uint16_t pin);
  bool isUp(uint16_t pin) const;
  void down(uint16_t pin);
  void downWhite();
};
