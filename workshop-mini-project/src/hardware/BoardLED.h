#pragma once
#include <Adafruit_NeoPixel.h>

/// @brief Built-IN Multicolor LED
class BoardLED
{
private:
  Adafruit_NeoPixel neoPixel;
  void setColor(uint8_t r, uint8_t g, uint8_t b);

public:
  BoardLED(uint8_t pin, uint16_t count = 1);
  void init();
  void red();
  void redYellow();
  void green();
  void yellow();
  void blue();
  void white();
  void off();
};
