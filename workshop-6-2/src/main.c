/*
 * Workshop 6.2 — Планувальник FreeRTOS: чотири задачі з різними пріоритетами.
 * Платформа: ESP32-C3 (одноядерний RISC-V), ESP-IDF.
 *
 *   1. Sensor Reader   (Normal)   — строго кожні 500 мс, xTaskDelayUntil
 *   2. Event Trigger   (Low)      — раз на 3 с динамічно створює Worker
 *   3. Worker Task     (High)     — друкує повідомлення і видаляє себе
 *   4. Maintenance     (Realtime) — раз на 10 с призупиняє/відновлює датчик
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "Config.h"

static TaskHandle_t s_sensor_task_handle = NULL;

/* stdout в ESP-IDF захищений власним м'ютексом усередині newlib.
 * Якщо призупинити задачу саме в момент, коли вона тримає цей м'ютекс,
 * наступний printf з іншої задачі заблокується назавжди (задачу вже
 * призупинено, і відновити її нікому — Maintenance стоїть на printf).
 * Тому Sensor друкує під власним м'ютексом, а Maintenance захоплює його
 * перед vTaskSuspend(): це гарантує, що датчик у цей момент не всередині
 * printf. М'ютекс FreeRTOS має успадкування пріоритету, тож інверсії немає. */
static SemaphoreHandle_t s_print_mutex = NULL;

/* ------------------------------------------------------------------------ */
/* 1. Sensor Reader — Normal, строга періодичність                          */
/* ------------------------------------------------------------------------ */
static void sensor_reader_task(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(SENSOR_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        if (xTaskDelayUntil(&last_wake, period) == pdFALSE)
        {
            last_wake = xTaskGetTickCount(); // Without this sensor will run multiple times
        }

        xSemaphoreTake(s_print_mutex, portMAX_DELAY);
        printf("[Sensor] Data read...\n");
        xSemaphoreGive(s_print_mutex);
    }
}

/* ------------------------------------------------------------------------ */
/* 3. Worker Task — High, одноразова, видаляє себе                          */
/* ------------------------------------------------------------------------ */
static void worker_task(void *pv)
{
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);
    printf("[Worker] Processing event...\n");
    xSemaphoreGive(s_print_mutex);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------------ */
/* 2. Event Trigger — Low, імітує зовнішню подію раз на 3 с                 */
/* ------------------------------------------------------------------------ */
static void event_trigger_task(void *pv)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(EVENT_PERIOD_MS));

        BaseType_t ok = xTaskCreate(worker_task, "Worker", STACK_WORKER,
                                    NULL, PRIO_WORKER, NULL);
        if (ok != pdPASS)
        {
            xSemaphoreTake(s_print_mutex, portMAX_DELAY);
            printf("[Event] Worker creation failed (out of heap)\n");
            xSemaphoreGive(s_print_mutex);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* 4. Maintenance Mode — Realtime, сервісний режим раз на 10 с              */
/* ------------------------------------------------------------------------ */
static void maintenance_task(void *pv)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(MAINTENANCE_PERIOD_MS));

        /* Захоплюємо м'ютекс друку -> датчик гарантовано поза printf. */
        xSemaphoreTake(s_print_mutex, portMAX_DELAY);
        vTaskSuspend(s_sensor_task_handle);
        xSemaphoreGive(s_print_mutex);

        xSemaphoreTake(s_print_mutex, portMAX_DELAY);
        printf("[Maintenance] System paused for %u ms.\n",
               (unsigned)MAINTENANCE_PAUSE_MS);
        xSemaphoreGive(s_print_mutex);

        vTaskDelay(pdMS_TO_TICKS(MAINTENANCE_PAUSE_MS));

        vTaskResume(s_sensor_task_handle);

        xSemaphoreTake(s_print_mutex, portMAX_DELAY);
        printf("[Maintenance] Sensor resumed.\n");
        xSemaphoreGive(s_print_mutex);
    }
}

/* ------------------------------------------------------------------------ */
void app_main(void)
{
    printf("\n=== Workshop 6.2: FreeRTOS scheduling demo (ESP32-C3) ===\n");

    s_print_mutex = xSemaphoreCreateMutex();
    configASSERT(s_print_mutex != NULL);

    /* Датчик створюємо першим: його хендл потрібен Maintenance-задачі. */
    BaseType_t ok = xTaskCreate(sensor_reader_task, "SensorReader",
                                STACK_SENSOR_READER, NULL,
                                PRIO_SENSOR_READER, &s_sensor_task_handle);
    configASSERT(ok == pdPASS);

    ok = xTaskCreate(event_trigger_task, "EventTrigger", STACK_EVENT_TRIGGER,
                     NULL, PRIO_EVENT_TRIGGER, NULL);
    configASSERT(ok == pdPASS);

    ok = xTaskCreate(maintenance_task, "Maintenance", STACK_MAINTENANCE,
                     NULL, PRIO_MAINTENANCE, NULL);
    configASSERT(ok == pdPASS);
}
