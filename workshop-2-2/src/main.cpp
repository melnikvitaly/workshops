#include <Arduino.h>
#include <LoopTracker.h>
#include <hardware/Debug.h>
#include <hardware/PWM.h>
#include <DelayMeasure.h>
#include <hardware/ADC.h>
#include <MotorController.h>
#pragma region PINS
constexpr uint8_t CONTROL_PIN = 5;
constexpr uint8_t IN_PIN_1 = 4;
constexpr uint8_t IN_PIN_2 = 3;
constexpr uint8_t ADC_MOTOR_SPEED_PIN = 0;
#pragma endregion

Debug dbg("main");
PWM motors(CONTROL_PIN, dbg.Scoped("control", NONE_LEVEL));
DelayMeasure measure1(IN_PIN_1, LOW, dbg.Scoped("m1-transistor-out"));
DelayMeasure measure2(IN_PIN_2, LOW, dbg.Scoped("m2-relay-out"));
ADC adc(ADC_MOTOR_SPEED_PIN, 12, dbg.Scoped("adc"), 5);

MotorController motorsController(motors, adc, measure1, measure2, dbg);

void setup()
{
  Serial.begin(115200);
  adc.init();
  motors.init();
  measure1.init();
  measure2.init();
  motorsController.begin();
  dbg.print("Started");
}

void loop()
{
  static LoopTracker loopTracker(dbg.Scoped("loop"));
  loopTracker.loopStart();

  motorsController.tick();

  loopTracker.loopEnd();
}
