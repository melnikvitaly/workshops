#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "hardware/ADC.hpp"
#include "hardware/PWM.hpp"
#include "hardware/Led.hpp"
#include "Sma.hpp"
#include "Ranges.hpp"
#include "Logger.hpp"

static PWM    pwm(GPIO_NUM_8, LEDC_CHANNEL_0, LEDC_TIMER_0);
static LED    led(pwm);
static ADC    adc(ADC_UNIT_1, ADC_CHANNEL_2, ADC_ATTEN_DB_12, 3100, ADC_BITWIDTH_DEFAULT);
static Sma<10> sma;
static Ranges  ranges(190, 210);
static Logger  logger("ADC_LED");

static uint8_t calculateDim(int value, int dark, int light)
{
    float t = (float)(value - dark) / (float)(light - dark);
    return (uint8_t)((1.0f - t) * 100.0f);
}

static void updateLed(int value)
{
    int dark  = ranges.darkThreshold();
    int light = ranges.lightThreshold();

    if (value < dark)
        led.on();
    else if (value > light)
        led.off();
    else
        led.power(calculateDim(value, dark, light));
}

static void processAdcLed()
{
    static int lastSmoothed = -1;
    int raw      = adc.readRaw();
    int smoothed = sma.update(raw);
    if (smoothed == lastSmoothed)
        return;
    lastSmoothed = smoothed;
    ranges.update(raw);

    updateLed(smoothed);

    logger.log(raw, smoothed,
               ranges.min(), ranges.darkThreshold(), ranges.lightThreshold(), ranges.max(),
               led.stateStr());
}

extern "C" void app_main()
{
    led.init();
    adc.init();
    logger.begin();

    while (1)
    {
        processAdcLed();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
