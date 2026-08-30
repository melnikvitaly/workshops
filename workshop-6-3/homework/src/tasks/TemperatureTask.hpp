#pragma once
#include <cstdint>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "../Clock.hpp"
#include "../Config.hpp"
#include "../SensorMessage.hpp"
#include "../hardware/BME280.hpp"

// Producer #1. Touches neither the consumer nor the other producer: the queue
// is the only thing the three tasks share.
class TemperatureTask
{
    static constexpr const char *TAG = "temp";

    BME280 &_sensor;
    QueueHandle_t _queue;
    TaskHandle_t _handle = nullptr;
    uint32_t _dropped = 0;

public:
    TemperatureTask(BME280 &sensor, QueueHandle_t queue) : _sensor(sensor), _queue(queue) {}

    void start()
    {
        xTaskCreate(&trampoline, "temp", Config::TASK_STACK_SIZE, this, Config::TEMP_PRIORITY, &_handle);
    }

private:
    static void trampoline(void *self) { static_cast<TemperatureTask *>(self)->run(); }

    [[noreturn]] void run()
    {
        // Period measured from the previous wake-up, so a slow read does not
        // drift the schedule.
        TickType_t lastWake = xTaskGetTickCount();
        for (;;)
        {
            float celsius = 0.0f;
            if (_sensor.readTemperature(celsius))
            {
                SensorMessage msg = {};
                msg.id = SensorId::Temperature;
                msg.timestampMs = nowMs();
                msg.value.celsius = celsius;
                send(msg);
            }
            xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::TEMP_PERIOD_MS));
        }
    }

    // Bounded wait, never portMAX_DELAY: a sensor blocked forever on a full
    // queue is a dead sensor, so a stalled consumer costs a sample instead.
    void send(const SensorMessage &msg)
    {
        if (xQueueSend(_queue, &msg, pdMS_TO_TICKS(Config::QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
        {
            _dropped++;
            ESP_LOGW(TAG, "queue full, dropped %lu total", static_cast<unsigned long>(_dropped));
        }
    }
};
