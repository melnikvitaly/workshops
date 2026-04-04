#include "LEDs.h"

LEDs::LEDs(uint8_t *pins, size_t count) : pins(pins), count(count) {}

void LEDs::init()
{
  for (size_t i = 0; i < count; i++)
  {
    pinMode(pins[i], OUTPUT);
  }
}

void LEDs::upAll()
{
  for (size_t i = 0; i < count; i++)
  {
    digitalWrite(pins[i], HIGH);
  }
}

void LEDs::downAll()
{
  for (size_t i = 0; i < count; i++)
  {
    digitalWrite(pins[i], LOW);
  }
}

void LEDs::setPin(size_t index, uint8_t level)
{
  if (index < count)
    digitalWrite(pins[index], level);
}

void LEDs::upRed() { setPin(RED_INDEX, HIGH); }
void LEDs::downRed() { setPin(RED_INDEX, LOW); }
void LEDs::upYellow() { setPin(YELLOW_INDEX, HIGH); }
void LEDs::downYellow() { setPin(YELLOW_INDEX, LOW); }
void LEDs::upGreen() { setPin(GREEN_INDEX, HIGH); }
void LEDs::downGreen() { setPin(GREEN_INDEX, LOW); }
