#include <Arduino.h>
#include <hardware/Led.h>
#include <LoopTracker.h>
#include <LedController.h>
#include <hardware/Button.h>
#include <hardware/Debug.h>

#pragma region PINS
constexpr LED::LEDPin LED_PIN = LED::LEDPin(8);
constexpr uint8_t BTN_PIN = 5;
#pragma endregion

constexpr bool USE_ISR_FOR_BUTTON = true;

Debug dbg("main");
Button btn(BTN_PIN, LOW, USE_ISR_FOR_BUTTON, dbg.Scoped("Btn"));
LED led(LED_PIN, dbg.Scoped("led"));
LedController ledController(led);

void setup()
{
  Serial.begin(115200);
  btn.init();
  led.init();

  ledController.start();
  btn.onPress([]
              { ledController.onChangeModePressed(); });
  dbg.print("Started");
}

void loop()
{
  static LoopTracker loopTracker(dbg.Scoped("loop"));
  loopTracker.loopStart();
  btn.tick();
  led.tick();
  ledController.tick();

  loopTracker.loopEnd();
}