#include <Arduino.h>
#include "hardware/BoardLED.h"
#include "hardware/LEDs.h"
#include "hardware/Button.h"
#include "hardware/DebugOut.h"
#include "TrafficLights.h"
#pragma region CONFIGURATION

constexpr ulong LEDS_RED = 17;
constexpr ulong LEDS_YELLOW = 16;
constexpr ulong LEDS_GREEN = 15;
constexpr ulong LEDS_WHITE = 35;
constexpr ulong LEDS_BUILDIN = 48;
constexpr ulong BAUD = 115200;
constexpr uint8_t BLUE_BTN_PIN = 40;
constexpr uint8_t BOOT_BTN_PIN = 0;
static constexpr uint8_t METHOD = 3;

#pragma endregion

static DebugOut dbg("APP");
static std::array<uint8_t, 3> trafficLedPins = {LEDS_RED, LEDS_YELLOW, LEDS_GREEN};
static std::array<uint8_t, 1> testPins = {LEDS_WHITE};
static BoardLED boardLed(LEDS_BUILDIN);
static LEDs trafficLeds(trafficLedPins.data(), trafficLedPins.size());
static LEDs testLeds(testPins.data(), testPins.size());
static TrafficLights trafficLights(trafficLeds, boardLed, dbg.Scoped(String("TL")));
static Button blueBtn(BLUE_BTN_PIN, LOW, dbg.Scoped("BTN" + String(BLUE_BTN_PIN)));
static Button bootBtn(BOOT_BTN_PIN, LOW, dbg.Scoped("BTN" + String(BOOT_BTN_PIN)));

void onAnyBtnClick()
{
  if (testLeds.isUp(LEDS_WHITE))
  {
    testLeds.down(LEDS_WHITE);
  }
  else
  {
    testLeds.up(LEDS_WHITE);
  }

  trafficLights.nextSpeed();
};

void onLongPressed()
{
  trafficLights.togglePause();
};

void setup()
{
  Serial.begin(BAUD);
  testLeds.init();
  trafficLeds.init();
  boardLed.init();
  trafficLights.init();
  blueBtn.init();
  bootBtn.init();
  blueBtn.onRelease(onAnyBtnClick);
  bootBtn.onRelease(onAnyBtnClick);
  blueBtn.onLongPress(onLongPressed);
  bootBtn.onLongPress(onLongPressed);
  dbg.print("initialized. Build: " __DATE__ " " __TIME__ " METHOD:" + String(METHOD));
}

// Method1, Long press does not work
void buttonsSyncProcess()
{
  auto blueBtnChange = blueBtn.wasStateChanged();
  auto bootBtnChanged = bootBtn.wasStateChanged();

  if (blueBtnChange == Button::BTN_CHANGE::Released || bootBtnChanged == Button::BTN_CHANGE::Released)
  {
    onAnyBtnClick();
  }
}

// Method2. buttonsSyncProcess + events.  Long press does not work
void buttonsUpdateSyncWithEvents()
{
  blueBtn.updateSyncWithEvents();
  bootBtn.updateSyncWithEvents();
}

// Method3. Async no delays
void buttonsTick()
{
  blueBtn.tick();
  bootBtn.tick();
}

void loop()
{
  if (METHOD == 1)
    buttonsSyncProcess();
  else if (METHOD == 2)
    buttonsUpdateSyncWithEvents();
  else if (METHOD == 3)
    buttonsTick();
  else
    dbg.print("UNKNOWN METHOD");

  trafficLights.tick();
  delay(1);
}
