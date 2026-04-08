#pragma once
#include <Arduino.h>
#include "hardware/AdditionalLED.h"
#include "hardware/LEDs.h"
#include "hardware/DebugOut.h"

class TrafficLights
{
public:
  enum class State : uint8_t
  {
    RED,
    RED_YELLOW,
    GREEN,
    GREEN_BLINK, //  turn off led on enter
    YELLOW
  };

private:
  BoardLED &additionalLed;
  LEDs &leds;
  DebugOut &debug;

  State state;
  bool isPaused;
  uint32_t stateStartMs;
  bool currentGreenLedState;
  std::array<float, 5> speedMultipliers = {.5f, 1.0f, 3.0f, 10.0f, 20.0f};
  uint8_t speedIndex = 1;
  uint8_t maxSpeedIndex = speedMultipliers.size() - 1;

  static constexpr uint32_t RED_MS = 3000;
  static constexpr uint32_t RED_YELLOW_MS = 1500;
  static constexpr uint32_t GREEN_MS = 3000;
  static constexpr size_t BLINKS_COUNT = 3;
  static constexpr uint32_t BLINK_PERIOD_MS = 1000;                              // on, off period (separately)
  static constexpr uint32_t GREEN_BLINK_MS = BLINKS_COUNT * 2 * BLINK_PERIOD_MS; // total green blinking duration

  static constexpr uint32_t YELLOW_MS = 1500;

  void enterState(State next);
  void processStateOnTick(uint32_t elapsed);

  const void blinkGreenLed(bool isOn);

  static const String stateToString(State c);
  const uint32_t adjustElapsedTimeAccordingToSpeed(uint32_t ms);

public:
  TrafficLights(LEDs &leds, BoardLED &additionalLed, DebugOut &debug);
  void init();
  void tick();
  void nextSpeed();
  void runAllStates();
  String stateInfo();
  void togglePause();
};
