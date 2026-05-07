#include <Arduino.h>
#include <hardware/Led.h>
#include <hardware/Button.h>
#include <hardware/ADC.h>
#include <hardware/PWM.h>
#include <hardware/Debug.h>
#include <LoopTracker.h>
#include <AlertController.h>

#pragma region PINS
constexpr uint8_t LED1_PIN = 4;
constexpr uint8_t LED2_PIN = 3;
constexpr uint8_t LED3_PIN = 2;
constexpr uint8_t BTN_PIN = 5;
constexpr uint8_t ADC_POT_PIN = 0;
#pragma endregion

Debug dbg("main");
PWM pwm1(LED1_PIN, dbg.Scoped("pwm1"), LEDC_CHANNEL_1, LEDC_TIMER_1);
PWM pwm2(LED2_PIN, dbg.Scoped("pwm2"), LEDC_CHANNEL_2, LEDC_TIMER_1);
PWM pwm3(LED3_PIN, dbg.Scoped("pwm3"), LEDC_CHANNEL_0, LEDC_TIMER_0);
LED led1(pwm1, dbg.Scoped("led1"));
LED led2(pwm2, dbg.Scoped("led2"));
LED led3(pwm3, dbg.Scoped("led3"));
Button btn(BTN_PIN, LOW, false, dbg.Scoped("btn"));
ADC pot(ADC_POT_PIN, 12, dbg.Scoped("pot"), 2);
AlertController alert(led1, led2, dbg.Scoped("alert"));

void setup()
{
  Serial.begin(115200);
  led1.init();
  led2.init();
  led3.init();
  pot.init();
  btn.init();

  btn.onPress([] { alert.onButtonPress(); });

  dbg.print("Started");
}

void loop()
{
  static LoopTracker loopTracker(dbg.Scoped("loop"));
  loopTracker.loopStart();

  btn.tick();
  alert.tick();
  led1.tick();
  led2.tick();
  led3.power(pot.percent());

  loopTracker.loopEnd();
}
