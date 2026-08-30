#pragma once
#include <cstdint>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "../Clock.hpp"
#include "../Config.hpp"
#include "../SensorMessage.hpp"
#include "../hardware/LDR.hpp"

class LightTask
{
    static constexpr const char *TAG = "light";

    LDR &_sensor;
    QueueHandle_t _queue;
    TaskHandle_t _handle = nullptr;
    uint32_t _dropped = 0;

public:
    LightTask(LDR &sensor, QueueHandle_t queue) : _sensor(sensor), _queue(queue) {}

    void start()
    {
        xTaskCreate(&trampoline, "light", Config::TASK_STACK_SIZE, this, Config::LIGHT_PRIORITY, &_handle);
    }

private:
    static void trampoline(void *self) { static_cast<LightTask *>(self)->run(); }

    [[noreturn]] void run()
    {
        TickType_t lastWake = xTaskGetTickCount();
        for (;;)
        {
            uint8_t percent = 0;
            if (_sensor.readPercent(percent))
            {
                SensorMessage msg = {};
                msg.id = SensorId::Light;
                msg.timestampMs = nowMs();
                msg.value.percent = percent;
                send(msg);
            }
            xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::LIGHT_PERIOD_MS));
        }
    }

    void send(const SensorMessage &msg)
    {
        if (xQueueSend(_queue, &msg, pdMS_TO_TICKS(Config::QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
        {
            _dropped++;
            ESP_LOGW(TAG, "queue full, dropped %lu total", static_cast<unsigned long>(_dropped));
        }
    }
};
