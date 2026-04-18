#include <Arduino.h>
#include "hardware/BoardLED.h"
#include "hardware/LEDs.h"
#include "hardware/Debug.h"
#include "TrafficLights.h"
#include "hardware/Button.h"
#include "hardware/L298NMotor.h"
#include "hardware/ADC.h"
#include "RPMCounter.h"
#include "SpeedSignal.h"

#pragma region PINS

constexpr uint8_t POTENTIOMETER_RED = 17;

constexpr uint8_t NONE_PIN = 45;
constexpr uint8_t LEDS_RED = 3;
constexpr uint8_t LEDS_YELLOW = 8;
constexpr uint8_t LEDS_GREEN = 18;

constexpr uint8_t M1E = 4;
constexpr uint8_t M11 = 5;
constexpr uint8_t M12 = 6;
constexpr uint8_t M21 = 7;
constexpr uint8_t M22 = 15;
constexpr uint8_t M2E = 16;

constexpr uint8_t LEDS_BUILDIN = 48;
constexpr uint8_t LEDS_FOR_MEASUREMENT = 9;
constexpr uint8_t LEDS_PHOTO_RESISTOR_1 = 10;
constexpr uint8_t LEDS_PHOTO_RESISTOR_2 = 12;
constexpr uint8_t BTN_MAIN = 40;
constexpr uint8_t BOOT_BTN_PIN = 0;
constexpr ulong BAUD = 115200;
constexpr ulong ADC_RESOLUTION = 1 << 12;
#pragma endregion

constexpr ulong POTENTIOMETER_STEP = 10;
static Debug dbg("APP");
static std::array<uint8_t, 3> ledPins = {LEDS_RED, LEDS_YELLOW, LEDS_GREEN};
static std::array<uint8_t, 3> motor1Pins = {M1E, M11, M12};
static std::array<uint8_t, 3> motor2Pins = {M2E, M21, M22};
static std::array<uint8_t, 1> measureLightLed = {LEDS_FOR_MEASUREMENT};

static BoardLED boardLed(LEDS_BUILDIN);
static BoardLED noneLed(NONE_PIN);
static LEDs trafficLightLeds(ledPins.data(), ledPins.size());
static LEDs measureLeds(measureLightLed.data(), measureLightLed.size());

static TrafficLights trafficLights(trafficLightLeds, noneLed, dbg.Scoped("TL"));

static Button mainBtn(BTN_MAIN, LOW, dbg.Scoped("BTN" + String(BTN_MAIN, NONE_LEVEL)));
static Button bootBtn(BOOT_BTN_PIN, LOW, dbg.Scoped("BTN" + String(BOOT_BTN_PIN, NONE_LEVEL)));

static L298NMotor motor1(motor1Pins, dbg.Scoped("M1", NONE_LEVEL));
static L298NMotor motor2(motor2Pins, dbg.Scoped("M2", NONE_LEVEL));

static ADC lightSensor1(LEDS_PHOTO_RESISTOR_1, ADC_RESOLUTION, dbg.Scoped("LS1"));
static ADC lightSensor2(LEDS_PHOTO_RESISTOR_2, ADC_RESOLUTION, dbg.Scoped("LS2"));

static RPMCounter m1Rpm(lightSensor1, dbg.Scoped("RPMCar"));
static RPMCounter m2Rpm(lightSensor2, dbg.Scoped("RPMPet"));

static SpeedSignal speedSignal(boardLed, dbg.Scoped("SpeedSignal"));
static ADC potentiometer(POTENTIOMETER_RED, ADC_RESOLUTION, dbg);

uint8_t currentMaxSpeed = L298NMotor::MAX_SPEED; // Current max speed allowed by potentiometer in %
uint8_t motorSpeed = currentMaxSpeed;            // Current motors' speed in %

#pragma region MOTORS CONTROL

void updateCurrentSpeed(uint8_t speedValue)
{
  motorSpeed = speedValue;
  motor1.speed(motorSpeed);
  motor2.speed(motorSpeed);
  dbg.print("new speed: " + String(motorSpeed));
}

void increaseMotorsSpeed()
{
  static uint8_t speedStep = 10;
  auto newSpeed = motorSpeed + speedStep;

  if (newSpeed > currentMaxSpeed)
  {
    newSpeed = speedStep;
  }
  updateCurrentSpeed(newSpeed);
}

#pragma endregion

#pragma region BUTTONs HANDLERS

void onBootBtnLongPress()
{
  updateCurrentSpeed(L298NMotor::MAX_SPEED);
}

void onBootBtnReleased()
{
  increaseMotorsSpeed();
}

void onMainBtnRelease()
{
  trafficLights.forceNextState();
}
void onMainBtnLongPress()
{
  trafficLights.togglePause();
}

#pragma endregion

void onLightTrafficStateChanged(TrafficLights::State state)
{
  static uint8_t num = 0;
  static L298NMotor::DIRECTION direction = L298NMotor::DIRECTION::FORWARD;
  switch (state)
  {
  case TrafficLights::State::RED_YELLOW:
  case TrafficLights::State::RED:
  case TrafficLights::State::YELLOW:
    motor1.move(L298NMotor::DIRECTION::STOP, 0);
    motor2.move(L298NMotor::DIRECTION::STOP, 0);
    break;
  case TrafficLights::State::GREEN:
    num++;
    direction = num % 2 == 0
                    ? L298NMotor::DIRECTION::BACKWARD
                    : L298NMotor::DIRECTION::FORWARD;
    motor1.move(direction, motorSpeed);
    motor2.move(direction, motorSpeed);
    break;
  case TrafficLights::State::GREEN_BLINK:
    motor1.move(direction, motorSpeed);
    motor2.move(direction, motorSpeed);
    break;
  default:
    dbg.print("ERR:UnknownSTATE");
    motor1.stop();
    motor2.stop();
    break;
  }
}

void setup()
{
  Serial.begin(BAUD);

  boardLed.init();
  trafficLightLeds.init();
  mainBtn.init();
  bootBtn.init();

  motor1.init();
  motor2.init();

  measureLeds.init();
  lightSensor1.init();
  lightSensor2.init();

  trafficLights.onNewState(onLightTrafficStateChanged);

  mainBtn.onRelease(onMainBtnRelease);
  mainBtn.onLongPress(onMainBtnLongPress);
  bootBtn.onRelease(onBootBtnReleased);
  bootBtn.onLongPress(onBootBtnLongPress);

  trafficLights.init();
  trafficLights.setSpeedMultiplier(0.5f);

  dbg.print("initialized.");

  measureLeds.upAll();
  speedSignal.track(&m1Rpm);
  speedSignal.track(&m2Rpm);

  motor1.forward(100);
  motor2.forward(10);
}

void processPotentiometer()
{
  auto value = potentiometer.percent();
  if (abs(value - currentMaxSpeed) < POTENTIOMETER_STEP)
  {
    return;
  }
  value = (value / POTENTIOMETER_STEP) * POTENTIOMETER_STEP; // adjust to step ladder
  auto previousMaxSpeed = currentMaxSpeed;
  currentMaxSpeed = value;
  dbg.print(String("CURRENTMAXSPEED(") + currentMaxSpeed + "%)");
  if (motorSpeed > currentMaxSpeed || motorSpeed == previousMaxSpeed)
  {
    dbg.print(String("speed=>CURRENTMAXSPEED(") + currentMaxSpeed + "%)");
    updateCurrentSpeed(currentMaxSpeed);
  }
}

void loop()
{
  // trafficLights.tick(); // tick/millis-based mode of traffic lights

  // mainBtn.tick();
  // bootBtn.tick();
  m1Rpm.tick();
  m2Rpm.tick();
  // speedSignal.tick();
  // processPotentiometer();
}
