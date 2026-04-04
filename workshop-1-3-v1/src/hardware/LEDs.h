#pragma once
#include <Arduino.h>

class LEDs
{
  static constexpr size_t RED_INDEX    = 0;
  static constexpr size_t YELLOW_INDEX = 1;
  static constexpr size_t GREEN_INDEX  = 2;

private:
  uint8_t *pins; // non-owning pointer — caller must keep the array alive
  size_t count;
  void setPin(size_t index, uint8_t level);

public:
  /// @brief  
  /// @param pins RED, YELLOW, GREEN, other 
  /// @param count number of passed pins 
  LEDs(uint8_t *pins, size_t count);
  void init();
  void upAll();
  void downAll();
  void upRed();
  void downRed();
  void upYellow();
  void downYellow();
  void upGreen();
  void downGreen();
};
