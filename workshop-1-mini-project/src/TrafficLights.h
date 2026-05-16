#pragma once
#include <Arduino.h>
#include "hardware/BoardLED.h"
#include "hardware/LEDs.h"
#include "hardware/Debug.h"

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

  using NewStateCallback = std::function<void(State)>;

private:
  BoardLED &additionalLed;
  LEDs &leds;
  Debug &debug;

  State state;
  NewStateCallback newStateEvent;
  uint32_t stateStartMs;
  bool currentGreenLedState;
  float speedMultiplier = 1.0;
  bool isPaused;
  static constexpr uint32_t RED_MS = 2000;
  static constexpr uint32_t RED_YELLOW_MS = 1500;
  static constexpr uint32_t GREEN_MS = 3000;
  static constexpr size_t BLINKS_COUNT = 3;
  static constexpr uint32_t BLINK_PERIOD_MS = 500;                              // on, off period (separately)
  static constexpr uint32_t GREEN_BLINK_MS = BLINKS_COUNT * 2 * BLINK_PERIOD_MS; // total green blinking duration

  static constexpr uint32_t YELLOW_MS = 1000;

  void enterState(State next);
  void processStateOnTick(uint32_t elapsed);

  const void blinkGreenLed(bool isOn);

  static const String stateToString(State c);
  const uint32_t adjustElapsedTimeAccordingToSpeed(uint32_t ms);

public:
  void togglePause();
  void setSpeedMultiplier(float value);
  TrafficLights(LEDs &leds, BoardLED &additionalLed, Debug &debug);
  void init();
  void tick();
  void runAllStates();
  void forceNextState();
  String stateInfo();

  void onNewState(NewStateCallback cb) { newStateEvent = cb; }
};
