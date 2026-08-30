#pragma once
#include <cstdint>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "../Config.hpp"
#include "../SensorMessage.hpp"

class ConsumerTask
{
    static constexpr const char *TAG = "consumer";

    QueueHandle_t _queue;
    TaskHandle_t _handle = nullptr;
    uint32_t _received = 0;

public:
    explicit ConsumerTask(QueueHandle_t queue) : _queue(queue) {}

    void start()
    {
        xTaskCreate(&trampoline, "consumer", Config::TASK_STACK_SIZE, this, Config::CONSUMER_PRIORITY, &_handle);
    }

private:
    static void trampoline(void *self) { static_cast<ConsumerTask *>(self)->run(); }

    [[noreturn]] void run()
    {
        for (;;)
        {
            SensorMessage msg = {};
            // portMAX_DELAY: blocked, not spinning — no CPU between messages.
            xQueueReceive(_queue, &msg, portMAX_DELAY);
            _received++;
            print(msg);
        }
    }

    void print(const SensorMessage &msg) const
    {
        switch (msg.id)
        {
        case SensorId::Temperature:
            // %f needs newlib float formatting, which ESP-IDF enables by default.
            ESP_LOGI(TAG, "[%lu ms] #%lu %s = %.2f C",
                     static_cast<unsigned long>(msg.timestampMs),
                     static_cast<unsigned long>(_received),
                     sensorName(msg.id),
                     msg.value.celsius);
            break;

        case SensorId::Light:
            ESP_LOGI(TAG, "[%lu ms] #%lu %s = %u %%",
                     static_cast<unsigned long>(msg.timestampMs),
                     static_cast<unsigned long>(_received),
                     sensorName(msg.id),
                     static_cast<unsigned>(msg.value.percent));
            break;
        }
    }
};
