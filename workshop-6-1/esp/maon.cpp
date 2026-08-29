#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED_PIN 18 // Пін світлодіода на DevKit

// Задача 1: Критичний процес (Heartbeat)
// Демонструємо, що незалежно від логера, світлодіод блимає рівно кожні 500мс
void heartbeat_task(void *pvParameter)
{
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    while (1)
    {
        gpio_set_level(LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500)); // Звільняємо CPU на 500 тіків (мс)
        gpio_set_level(LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Задача 2: Фоновий процес (Logger)
// Демонструємо роботу з інтервалом в 1 секунду
void logger_task(void *pvParameter)
{
    uint32_t seconds = 0;

    while (1)
    {
        printf("[Logger] Система працює. Uptime: %lu сек\n", seconds++);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Абсолютно не заважає Heartbeat-задачі
    }
}

void app_main(void)
{
    printf("Старт системи. Ініціалізація FreeRTOS...\n");

    // Створюємо задачі та саджаємо їх на Core 1 (вільне від системних Wi-Fi задач ядро)
    xTaskCreatePinnedToCore(heartbeat_task, "Heartbeat", 2048, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(logger_task, "Logger", 2048, NULL, 4, NULL, 1);

    // app_main завершується, але задачі продовжують працювати у фоні
}