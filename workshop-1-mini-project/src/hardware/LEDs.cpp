#include "LEDs.h"

LEDs::LEDs(uint8_t *pins, size_t count)
    : pins(pins), state(new uint8_t[count]{}), count(count) {}

LEDs::~LEDs() { delete[] state; }

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
    state[i] = HIGH;
  }
}

void LEDs::downAll()
{
  for (size_t i = 0; i < count; i++)
  {
    digitalWrite(pins[i], LOW);
    state[i] = LOW;
  }
}

size_t LEDs::findIndex(uint16_t pin) const
{
  for (size_t i = 0; i < count; i++)
    if (pins[i] == pin) return i;
  return count; // not found
}

void LEDs::up(uint16_t pin)
{
  size_t i = findIndex(pin);
  if (i < count) { digitalWrite(pins[i], HIGH); state[i] = HIGH; }
}

bool LEDs::isUp(uint16_t pin) const
{
  size_t i = findIndex(pin);
  return (i < count) && (state[i] == HIGH);
}

void LEDs::down(uint16_t pin)
{
  size_t i = findIndex(pin);
  if (i < count) { digitalWrite(pins[i], LOW); state[i] = LOW; }
}

void down(uint16_t pin){
  digitalWrite(pin, LOW);
}

void LEDs::setPin(size_t index, uint8_t level)
{
  if (index < count) {
    digitalWrite(pins[index], level);
    state[index] = level;
  }
}

void LEDs::upRed() { setPin(RED_INDEX, HIGH); }
void LEDs::downRed() { setPin(RED_INDEX, LOW); }
void LEDs::upYellow() { setPin(YELLOW_INDEX, HIGH); }
void LEDs::downYellow() { setPin(YELLOW_INDEX, LOW); }
void LEDs::upGreen() { setPin(GREEN_INDEX, HIGH); }
void LEDs::downGreen() { setPin(GREEN_INDEX, LOW); }
