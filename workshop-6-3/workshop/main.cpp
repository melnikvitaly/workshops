#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#define DEMO_MODE_FAIL 0

#if DEMO_MODE_FAIL == 1

static const char *TAG = "RACE_CONDITION";

volatile uint32_t shared_counter = 0;
volatile bool start_gun = false;

// Прапорці для синхронізації (щоб main знав, коли таски завершать роботу)
volatile bool task0_done = false;
volatile bool task1_done = false;

const uint32_t ITERATIONS = 100000;

// --- ТАСКА НА ЯДРІ 0 ---
void task_increment_core0(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] Готовий до старту...", xPortGetCoreID());
    // Чекаємо, поки app_main не дасть відмашку
    while (!start_gun)
    {
        vTaskDelay(1);
    }
    ESP_LOGI(TAG, "[Core %d] Запуск таски 0...", xPortGetCoreID());

    for (uint32_t i = 0; i < ITERATIONS; i++)
    {

        uint32_t temp = shared_counter;
        temp = temp + 1;
        shared_counter = temp;
    }

    ESP_LOGI(TAG, "[Core %d] Таска 0 завершила роботу.", xPortGetCoreID());
    task0_done = true;
    vTaskDelete(NULL); // Знищуємо таску після виконання
}

// --- ТАСКА НА ЯДРІ 1 ---
void task_increment_core1(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] Готовий до старту...", xPortGetCoreID());
    // Чекаємо, поки app_main не дасть відмашку
    while (!start_gun)
    {
        vTaskDelay(1);
    }
    ESP_LOGI(TAG, "[Core %d] Запуск таски 1...", xPortGetCoreID());

    for (uint32_t i = 0; i < ITERATIONS; i++)
    {
        // Робимо те саме на сусідньому ядрі
        uint32_t temp = shared_counter;
        temp = temp + 1;
        shared_counter = temp;
    }

    ESP_LOGI(TAG, "[Core %d] Таска 1 завершила роботу.", xPortGetCoreID());
    task1_done = true;
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== СТАРТ ЕКСПЕРИМЕНТУ ===");

    // Запускаємо таски і жорстко прив'язуємо їх до різних ядер (0 та 1)
    // Пріоритет однаковий (5), щоб вони працювали абсолютно паралельно
    xTaskCreatePinnedToCore(task_increment_core0, "Task0", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_increment_core1, "Task1", 2048, NULL, 5, NULL, 1);

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "=== ПОСТРІЛ СТАРТОВОГО ПІСТОЛЕТА! ===");
    start_gun = true;

    while (!task0_done || !task1_done)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "=== РЕЗУЛЬТАТИ ===");
    ESP_LOGI(TAG, "Очікувалося: %lu (100k + 100k)", ITERATIONS * 2);

    if (shared_counter == (ITERATIONS * 2))
    {
        ESP_LOGI(TAG, "Фактично: %lu -> Диво! Стан гонки не відбувся.", shared_counter);
    }
    else
    {
        ESP_LOGE(TAG, "Фактично: %lu -> СТАН ГОНКИ ЗЛОВЛЕНО!", shared_counter);
        ESP_LOGE(TAG, "Втрачено інкрементів: %lu", (ITERATIONS * 2) - shared_counter);
    }
}
#else

#define BOOT_BUTTON_PIN GPIO_NUM_0

static const char *TAG = "QUEUE_ISR_DEMO";

QueueHandle_t xIncrementQueue;
const uint32_t ITERATIONS = 10000;
volatile bool start_gun = false;
volatile uint32_t last_button_press_time = 0;

void IRAM_ATTR boot_button_isr_handler(void *arg)
{
    uint32_t current_time = xTaskGetTickCountFromISR();

    if (current_time - last_button_press_time > pdMS_TO_TICKS(200))
    {

        if (gpio_get_level(BOOT_BUTTON_PIN) == 0)
        {

            uint8_t button_event = 100;
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;

            xQueueSendFromISR(xIncrementQueue, &button_event, &xHigherPriorityTaskWoken);

            last_button_press_time = current_time;

            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

// --- ТАСКА-ПРОДЮСЕР ---
void task_producer(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] Готовий до старту...", xPortGetCoreID());
    // Чекаємо, поки app_main не дасть відмашку
    while (!start_gun)
    {
        vTaskDelay(1);
    }
    int core_id = xPortGetCoreID();
    uint8_t dummy_event = 1; // Звичайна подія (+1)

    ESP_LOGI(TAG, "[Core %d] Продюсер стартував...", core_id);

    for (uint32_t i = 0; i < ITERATIONS; i++)
    {
        xQueueSend(xIncrementQueue, &dummy_event, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "[Core %d] Продюсер %s закінчив відправку.", core_id, (char *)pvParameters);
    vTaskDelete(NULL);
}

// --- ТАСКА-КОНСЬЮМЕР ---
void task_consumer(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] Готовий до старту...", xPortGetCoreID());
    // Чекаємо, поки app_main не дасть відмашку
    while (!start_gun)
    {
        vTaskDelay(1);
    }
    ESP_LOGI(TAG, "[Core %d] Консьюмер стартував і чекає...", xPortGetCoreID());

    uint32_t safe_counter = 0;
    uint8_t rx_data;
    // Тепер це нескінченний цикл, як у реальному пристрої
    while (1)
    {
        xQueueReceive(xIncrementQueue, &rx_data, portMAX_DELAY);

        if (rx_data == 1)
        {
            safe_counter++; // Подія від таски

            // Логуємо прогрес, щоб бачити, що система жива
            if (safe_counter == (ITERATIONS * 2))
            {
                ESP_LOGI(TAG, "Усі таски відпрацювали! Базовий лічильник: %lu", safe_counter);
            }
        }
        else if (rx_data == 100)
        {
            // Подія від апаратного переривання (кнопки)
            ESP_LOGW(TAG, "⚡ АЛАРМ! Натиснуто BOOT кнопку! Поточний лічильник: %lu", safe_counter);
        }
    }
}

void init_hardware()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE};
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BOOT_BUTTON_PIN, boot_button_isr_handler, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== СТАРТ СИСТЕМИ З ПЕРЕРИВАННЯМИ ===");

    init_hardware();

    xIncrementQueue = xQueueCreate(100, sizeof(uint8_t));
    if (xIncrementQueue == NULL)
        abort();

    // Запускаємо Консьюмера з найвищим пріоритетом (10), щоб він миттєво реагував на кнопку
    xTaskCreatePinnedToCore(task_consumer, "Consumer", 4096, NULL, 10, NULL, tskNO_AFFINITY);
    // Продюсери мають нижчий пріоритет (5)
    xTaskCreatePinnedToCore(task_producer, "Prod1", 4096, (void *)"№1", 5, NULL, 0);
    xTaskCreatePinnedToCore(task_producer, "Prod2", 4096, (void *)"№2", 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "=== ПОСТРІЛ СТАРТОВОГО ПІСТОЛЕТА! ===");
    start_gun = true;
}
#endif