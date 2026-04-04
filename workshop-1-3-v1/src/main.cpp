#include <Arduino.h>
#include "hardware/AdditionalLED.h"
#include "hardware/LEDs.h"
#include "hardware/DebugOut.h"
#include "TrafficLights.h"

#pragma region CONFIGURATION

constexpr ulong LEDS_RED = 17;
constexpr ulong LEDS_YELLOW = 16;
constexpr ulong LEDS_GREED = 15;
constexpr ulong LEDS_BUILDIN = 48;
constexpr ulong BAUD = 115200;

#pragma endregion

static DebugOut debug("APP");
static std::array<uint8_t, 3> ledPins = {LEDS_RED, LEDS_YELLOW, LEDS_GREED};
static AdditionalLED additionalLed(LEDS_BUILDIN);
static LEDs leds(ledPins.data(), ledPins.size());
static TrafficLights trafficLights(leds, additionalLed, debug.Scoped("TL"));

void setup()
{
  Serial.begin(BAUD);
  trafficLights.init();
  debug.print("initialized.");
}

void loop()
{
  trafficLights.runAllStates(); // synchronous mode of traffic lights
  // trafficLights.tick();  // tick/millis-based mode of traffic lights
  delay(1);
}
