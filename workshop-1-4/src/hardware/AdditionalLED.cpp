#include "AdditionalLED.h"

BoardLED::BoardLED(uint8_t pin, uint16_t count)
    : neoPixel(count, pin, NEO_GRB + NEO_KHZ800) {}

void BoardLED::init()
{
  neoPixel.begin();
  neoPixel.setBrightness(10);
  neoPixel.show();
}

void BoardLED::setColor(uint8_t r, uint8_t g, uint8_t b)
{
  neoPixel.setPixelColor(0, neoPixel.Color(r, g, b));
  neoPixel.show();
}

void BoardLED::red()
{
  setColor(255, 0, 0);
}

void BoardLED::redYellow()
{
  setColor(255, 80, 0);
}

void BoardLED::green()
{
  setColor(0, 255, 0);
}

void BoardLED::yellow()
{
  setColor(255, 120, 0);
}

void BoardLED::blue()
{
  setColor(0, 0, 255);
}

void BoardLED::white()
{
  setColor(255, 255, 255);
}

void BoardLED::off()
{
  setColor(0, 0, 0);
}
