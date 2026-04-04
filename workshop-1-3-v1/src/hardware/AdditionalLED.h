#pragma once
#include <Adafruit_NeoPixel.h>

/// @brief Built-IN Multicolor LED
class AdditionalLED
{
private:
  Adafruit_NeoPixel neoPixel;

public:
  AdditionalLED(uint8_t pin, uint16_t count = 1);
  void init();
  void red();
  void redYellow();
  void green();
  void yellow();
  void off();
};
