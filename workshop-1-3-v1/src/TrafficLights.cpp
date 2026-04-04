#include "TrafficLights.h"

TrafficLights::TrafficLights(LEDs &leds, AdditionalLED &additionalLed, DebugOut &debug)
    : additionalLed(additionalLed), leds(leds),
      state(State::RED), stateStartMs(0), debug(debug) {}

#pragma region General

void TrafficLights::init()
{
  leds.init();
  additionalLed.init();
  enterState(State::RED);
}

void TrafficLights::enterState(State newState)
{
  debug.print(stateToString(state) + "->" + stateToString(newState));
  state = newState;
  stateStartMs = millis();

  leds.downAll(); // maybe better to avoid this.

  switch (newState)
  {
  case State::RED:
    leds.upRed();
    additionalLed.red();
    break;

  case State::RED_YELLOW:
    leds.upRed();
    leds.upYellow();
    additionalLed.redYellow();
    break;

  case State::GREEN:
    leds.upGreen();
    additionalLed.green();
    break;

  case State::GREEN_BLINK:
    leds.downGreen();
    additionalLed.off();
    break;

  case State::YELLOW:
    leds.upYellow();
    additionalLed.yellow();
    break;
  }
}

const void TrafficLights::blinkGreenLed(bool ledOn)
{
  if (ledOn)
  {
    leds.upGreen();
    additionalLed.green();
  }
  else
  {
    leds.downGreen();
    additionalLed.off();
  }
}

#pragma endregion

#pragma region Sync Processing

void TrafficLights::runAllStates()
{
  debug.print(String("Iterate states"));
  enterState(State::RED);
  delay(RED_MS);
  enterState(State::RED_YELLOW);
  delay(RED_YELLOW_MS);
  enterState(State::GREEN);
  delay(GREEN_MS);
  enterState(State::GREEN_BLINK);
  for (size_t blinksCount = 0; blinksCount < BLINKS_COUNT; blinksCount++)
  {
    debug.print(String("Blink#") + blinksCount);
    delay(BLINK_PERIOD_MS);
    blinkGreenLed(true);
    delay(BLINK_PERIOD_MS);
    blinkGreenLed(false);
  }

  enterState(State::YELLOW);
  delay(YELLOW_MS);
}

#pragma endregion

#pragma region Async Ticks Processing

void TrafficLights::processStateOnTick(uint32_t elapsedInState)
{
  switch (state)
  {
  case State::RED:
    if (elapsedInState >= RED_MS)
      enterState(State::RED_YELLOW);
    break;

  case State::RED_YELLOW:
    if (elapsedInState >= RED_YELLOW_MS)
      enterState(State::GREEN);
    break;

  case State::GREEN:
    if (elapsedInState >= GREEN_MS)
      enterState(State::GREEN_BLINK);
    break;

  case State::GREEN_BLINK:
  {
    if (elapsedInState >= GREEN_BLINK_MS)
    {
      enterState(State::YELLOW);
      break;
    }

    bool ledOn = ((elapsedInState / BLINK_PERIOD_MS) % 2) != 0;
    if (currentGreenLedState == ledOn) // not sure if its really critical though.
    {
      break;
    }
    currentGreenLedState = ledOn;
    blinkGreenLed(ledOn);
    break;
  }

  case State::YELLOW:
    if (elapsedInState >= YELLOW_MS)
      enterState(State::RED);
    break;
  }
}

void TrafficLights::tick()
{
  uint32_t elapsed = millis() - stateStartMs;
  processStateOnTick(elapsed);
}

#pragma endregion

#pragma region State Helpers

String TrafficLights::stateInfo()
{
  return stateToString(state);
}

const String TrafficLights::stateToString(State state)
{
  String name;
  switch (state)
  {
  case State::RED:
    name = "RED";
    break;
  case State::GREEN:
    name = "GREEN";
    break;
  case State::YELLOW:
    name = "YELLOW";
    break;
  case State::GREEN_BLINK:
    name = "GREEN_BLINK";
    break;
  case State::RED_YELLOW:
    name = "RED_YELLOW";
    break;
  default:
    name = "Unknown";
    break;
  }

  return String(name) + "(" + static_cast<uint8_t>(state) + ")";
}

#pragma endregion