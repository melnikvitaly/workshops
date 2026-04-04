#include "AdditionalLED.h"

AdditionalLED::AdditionalLED(uint8_t pin, uint16_t count)
    : neoPixel(count, pin, NEO_GRB + NEO_KHZ800) {}

void AdditionalLED::init()
{
  neoPixel.begin();
  neoPixel.setBrightness(10);
  neoPixel.show();
}

void AdditionalLED::red()
{
  neoPixel.setPixelColor(0, neoPixel.Color(255, 0, 0));
  neoPixel.show();
}

void AdditionalLED::redYellow()
{
  neoPixel.setPixelColor(0, neoPixel.Color(255, 80, 0));
  neoPixel.show();
}

void AdditionalLED::green()
{
  neoPixel.setPixelColor(0, neoPixel.Color(0, 255, 0));
  neoPixel.show();
}

void AdditionalLED::yellow()
{
  neoPixel.setPixelColor(0, neoPixel.Color(255, 120, 0));
  neoPixel.show();
}

void AdditionalLED::off()
{
  neoPixel.setPixelColor(0, neoPixel.Color(0, 0, 0));
  neoPixel.show();
}
