#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "hardware/ADC.hpp"
#include "hardware/PWM.hpp"
#include "hardware/Led.hpp"
#include "hardware/Motor.hpp"
#include "Sma.hpp"
#include "Ranges.hpp"
#include "Logger.hpp"

static constexpr uint8_t MOTOR_CUT_OFF = 5;
static constexpr uint8_t MOTOR_CUT_ON  = 95;
static constexpr uint8_t MOTOR_MIN_PCT = 65;
static constexpr uint8_t MOTOR_MAX_PCT = 100;

static PWM    ledPwm(GPIO_NUM_8,  LEDC_CHANNEL_0, LEDC_TIMER_0);
static PWM    motPwm(GPIO_NUM_35, LEDC_CHANNEL_1, LEDC_TIMER_1);
static LED    led(ledPwm);
static Motor  motor(motPwm, MOTOR_CUT_OFF, MOTOR_CUT_ON, MOTOR_MIN_PCT, MOTOR_MAX_PCT);
static ADC    adc(ADC_UNIT_1, ADC_CHANNEL_2, ADC_ATTEN_DB_12);
static Sma<10> sma;
static Ranges  ranges;
static Logger  logger("POT_CTRL");

static void processAdc()
{
    static int lastSmoothed = -1;
    int raw      = adc.readRaw();
    int smoothed = sma.update(raw);
    if (smoothed == lastSmoothed)
        return;
    lastSmoothed = smoothed;

    ranges.update(raw);
    uint8_t ledPct   = adc.pct(smoothed);
    uint8_t motorPct = motor.powerMapped(ledPct);
    led.power(ledPct);

    logger.log(raw, smoothed, ranges.min(), ranges.max(), ledPct, motorPct);
}

extern "C" void app_main()
{
    led.init();
    motor.init();
    adc.init();
    logger.begin();

    while (1)
    {
        processAdc();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
