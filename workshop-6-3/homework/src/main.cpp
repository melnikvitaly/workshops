#include <cstdlib>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "Config.hpp"
#include "SensorMessage.hpp"
#include "hardware/BME280.hpp"
#include "hardware/I2CBus.hpp"
#include "hardware/LDR.hpp"
#include "tasks/ConsumerTask.hpp"
#include "tasks/LightTask.hpp"
#include "tasks/TemperatureTask.hpp"

#pragma region WIRING
// BME280  SDA -> GPIO8, SCL -> GPIO9, VCC -> 3V3, GND -> GND (+ 4.7k pull-ups)
// LDR     divider output -> GPIO0 (ADC1_CH0)
#pragma endregion

static constexpr const char *TAG = "main";

static I2CBus i2c;
static BME280 bme280(i2c);
static LDR ldr;

extern "C" void app_main()
{
    // Whole messages, not pointers: the queue copies by value, which is what
    // makes the hand-off safe without a mutex.
    QueueHandle_t sensorQueue = xQueueCreate(Config::QUEUE_LENGTH, sizeof(SensorMessage));
    if (sensorQueue == nullptr)
    {
        ESP_LOGE(TAG, "queue allocation failed");
        abort();
    }

    const bool bme280Ready = bme280.init();
    const bool ldrReady = ldr.init();

    // Static, not automatic: app_main returns while the tasks keep running.
    static ConsumerTask consumer(sensorQueue);
    static TemperatureTask temperature(bme280, sensorQueue);
    static LightTask light(ldr, sensorQueue);

    // Consumer first, so a receiver exists before anything can be sent.
    consumer.start();

    // A dead sensor costs its own producer, nothing else.
    if (bme280Ready)
        temperature.start();
    else
        ESP_LOGW(TAG, "BME280 missing, temperature producer not started");

    if (ldrReady)
        light.start();
    else
        ESP_LOGW(TAG, "LDR init failed, light producer not started");

    ESP_LOGI(TAG, "started");
}
