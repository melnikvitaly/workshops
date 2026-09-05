#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "PRO_ARCH";

// ==============================================================================
// БЛОК 4: Обов'язковий Hook. Викликається автоматично ядром, якщо пам'ять закінчилась
// (Вимагає увімкнення у menuconfig: configUSE_MALLOC_FAILED_HOOK)
// ==============================================================================
void vApplicationMallocFailedHook(void) {
    ESP_LOGE("HEAP_PANIC", "MALLOC FAILED! Free: %zu, Largest: %zu",
             heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    // Тут ми можемо зберегти лог у флеш, смикнути GPIO-пін для сигналізації і піти в ребут
    configASSERT(0); 
}

// ==============================================================================
// БЛОК 3: Статичні ресурси (Ніякого heap'у, виділяється компілятором)
// ==============================================================================
#define QUEUE_LENGTH 10
#define ITEM_SIZE sizeof(uint32_t)
static uint8_t queue_storage[QUEUE_LENGTH * ITEM_SIZE];
static StaticQueue_t queue_tcb;
QueueHandle_t sensor_queue; // Глобальний хендл черги

#define SENSOR_STACK_SIZE 3072
static StackType_t sensor_stack[SENSOR_STACK_SIZE];
static StaticTask_t sensor_tcb;


// ==============================================================================
// БЛОК 4: Правильна робота з ISR. НІЯКИХ malloc/free в перериваннях!
// ==============================================================================
void IRAM_ATTR dummy_isr_handler(void *arg) {
    uint32_t hardware_data = 42; // Імітація зчитаних даних з регістра
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Тільки сигналізація! Кидаємо дані в чергу і швидко тікаємо
    xQueueSendFromISR(sensor_queue, &hardware_data, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ==============================================================================
// БЛОК 5: Безпечний Task. Буфери виділені 1 раз, жодних виділень у циклі while(1)
// ==============================================================================
void vSensorTask(void *pvParameters) {
    // 1. Pre-allocation: Виділяємо специфічну пам'ять ОДИН РАЗ на старті таски
    
    // MALLOC_CAP_DMA (Блок 1, 6): Цей буфер гарантовано вирівняний для роботи з апаратним SPI/I2S
    uint8_t *dma_buffer = heap_caps_malloc(1024, MALLOC_CAP_DMA);
    if (dma_buffer == NULL) {
        ESP_LOGE(TAG, "Hardware failure: DMA buffer alloc failed");
        vTaskDelete(NULL); // Soft fail стратегія
    }

    uint32_t received_data;
    
    ESP_LOGI(TAG, "Sensor Task Started safely");

    // 2. Цикл життя: тут немає malloc() і немає free()! Тільки перевикористання.
    while (1) {
        if (xQueueReceive(sensor_queue, &received_data, portMAX_DELAY)) {
            // Очищуємо старі дані (замість того, щоб звільняти і виділяти новий буфер)
            memset(dma_buffer, 0, 1024); 
            
            // ... тут йде обробка DMA та відправка ...
            // Імітація роботи:
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }
    }
}

// ==============================================================================
// МОНІТОРИНГ СИСТЕМИ (Блоки 1, 2, 3)
// ==============================================================================
#include "esp_timer.h" // Потрібно для esp_timer_get_time()

void vMonitorTask(void *pvParameters) {
    TaskHandle_t sensor_handle = (TaskHandle_t)pvParameters;

    while (1) {
        ESP_LOGI(TAG, "=== SYSTEM HEALTH ===");
        
        // 1. HWM стеку
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(sensor_handle);
        float hwm_percent = ((float)hwm / SENSOR_STACK_SIZE) * 100.0f;
        ESP_LOGI(TAG, "[STACK] Sensor HWM: %.1f%% (%u words free)", hwm_percent, hwm);

        if (hwm_percent < 10.0f) ESP_LOGE(TAG, "CRITICAL: Stack < 10%%!");

        // 2. Аналіз фрагментації
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "[HEAP] Free Internal: %zu B | Largest Block: %zu B", free_internal, largest_block);
        
        if (largest_block < (free_internal / 2)) {
            ESP_LOGW(TAG, "WARNING: Heap fragmentation detected!");
        }

        // 3. Вимірювання часу алокації (Бенчмарк)
        // Виділяємо типовий маленький блок (наприклад, 64 байти для MQTT-повідомлення)
        int64_t start_time = esp_timer_get_time();
        void *test_ptr = heap_caps_malloc(64, MALLOC_CAP_INTERNAL);
        int64_t end_time = esp_timer_get_time();

        if (test_ptr != NULL) {
            ESP_LOGI(TAG, "[PERF] 64-byte allocation took: %lld microseconds", (end_time - start_time));
            heap_caps_free(test_ptr); // Одразу віддаємо назад, щоб не робити витік
        } else {
            ESP_LOGE(TAG, "[PERF] Allocation failed during benchmark!");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ==============================================================================
// ІНІЦІАЛІЗАЦІЯ (Блок 5: Зробити максимум до запуску планувальника)
// ==============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "System Booting...");

    // 1. Статична черга (Блок 2, 5) - нуль фрагментації
    sensor_queue = xQueueCreateStatic(QUEUE_LENGTH, ITEM_SIZE, queue_storage, &queue_tcb);

    // 2. Виділяємо пам'ять під важкі завдання у PSRAM до старту системи (Блок 1, 6)
    // Якщо PSRAM вимкнена, це поверне NULL
    uint8_t *video_frame = heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM);
    if (video_frame != NULL) {
        ESP_LOGI(TAG, "PSRAM Video buffer allocated successfully");
    } else {
        ESP_LOGW(TAG, "PSRAM buffer failed (Is PSRAM enabled in menuconfig?)");
    }

    // 3. Статична таска для критичної логіки (Блок 3)
    TaskHandle_t sensor_handle = xTaskCreateStatic(
        vSensorTask, "Sensor", SENSOR_STACK_SIZE, NULL, 5, sensor_stack, &sensor_tcb
    );

    // 4. Динамічна таска для некритичного моніторингу
    xTaskCreate(vMonitorTask, "Monitor", 3072, (void*)sensor_handle, 2, NULL);
    
    ESP_LOGI(TAG, "Initialization complete. Entering run-time.");
}