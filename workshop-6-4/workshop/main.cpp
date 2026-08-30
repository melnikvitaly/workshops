#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BOOT_BUTTON_PIN GPIO_NUM_0

static const char *TAG = "FSM_PRO";

// ==========================================
// 1. АБСТРАКЦІЇ (Словник нашої системи)
// ==========================================

// Стани Головного Автомата (Сигналізація)
typedef enum {
    ALARM_STATE_DISARMED,
    ALARM_STATE_ARMED,
    ALARM_STATE_ALARMING
} alarm_state_t;

// Події, які генерує Кнопка
typedef enum {
    EVENT_NONE,
    EVENT_SHORT_PRESS,
    EVENT_LONG_PRESS
} system_event_t;

// Стани Автомата Кнопки (Антибрязкіт + Таймінг)
typedef enum {
    BTN_STATE_IDLE,
    BTN_STATE_DEBOUNCE,
    BTN_STATE_PRESSED,
    BTN_STATE_WAIT_RELEASE
} btn_state_t;

QueueHandle_t xEventQueue;

// ==========================================
// 2. АВТОМАТ КНОПКИ (Producer)
// Працює кожні 10 мс, не боїться жодних шумів і ЕМП
// ==========================================
void task_button_fsm(void *pvParameters) {
    btn_state_t btn_state = BTN_STATE_IDLE;
    uint32_t hold_time = 0;
    system_event_t event_to_send;

    while (1) {
        int level = gpio_get_level(BOOT_BUTTON_PIN);
        
        // Подвійний switch: Стан -> Фізичний рівень піна / Час
        switch (btn_state) {
            
            case BTN_STATE_IDLE:
                switch (level) {
                    case 0: // Побачили нуль - починаємо перевірку
                        btn_state = BTN_STATE_DEBOUNCE;
                        hold_time = 0;
                        break;
                    default: break;
                }
                break;

            case BTN_STATE_DEBOUNCE:
                hold_time += 10; // Додаємо 10 мс
                switch (hold_time) {
                    case 50: // Пройшло 50 мс
                        switch (level) {
                            case 0: // Кнопка все ще натиснута! Це не шум.
                                btn_state = BTN_STATE_PRESSED;
                                hold_time = 0;
                                break;
                            case 1: // Це був просто шум (відскок)
                                btn_state = BTN_STATE_IDLE; 
                                break;
                        }
                        break;
                    default: break;
                }
                break;

            case BTN_STATE_PRESSED:
                hold_time += 10;
                switch (level) {
                    case 1: // Відпустили раніше, ніж за 1000 мс -> Коротке натискання
                        event_to_send = EVENT_SHORT_PRESS;
                        xQueueSend(xEventQueue, &event_to_send, 0);
                        btn_state = BTN_STATE_IDLE;
                        break;
                    
                    case 0: // Все ще тримають...
                        switch (hold_time) {
                            case 1000: // Досягли 1 секунди! -> Довге натискання
                                event_to_send = EVENT_LONG_PRESS;
                                xQueueSend(xEventQueue, &event_to_send, 0);
                                btn_state = BTN_STATE_WAIT_RELEASE;
                                break;
                            default: break;
                        }
                        break;
                }
                break;

            case BTN_STATE_WAIT_RELEASE:
                // Чекаємо, поки користувач прибере палець після довгого натискання
                switch (level) {
                    case 1: 
                        btn_state = BTN_STATE_IDLE; 
                        break;
                    default: break;
                }
                break;
        }

        // Засинаємо рівно на 10 мс (такт нашого автомата)
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==========================================
// 3. АВТОМАТ СИГНАЛІЗАЦІЇ (Consumer)
// ==========================================
void task_alarm_fsm(void *pvParameters) {
    alarm_state_t current_state = ALARM_STATE_DISARMED;
    system_event_t current_event;
    
    ESP_LOGI(TAG, "🟢 Систему запущено. Стан: DISARMED.");

    while (1) {
        xQueueReceive(xEventQueue, &current_event, portMAX_DELAY);

        switch (current_state) {
            
            case ALARM_STATE_DISARMED:
                switch (current_event) {
                    case EVENT_SHORT_PRESS:
                        ESP_LOGW(TAG, "🛡️ ОХОРОНУ УВІМКНЕНО!");
                        current_state = ALARM_STATE_ARMED;
                        break;
                    default: break; // Ігноруємо решту подій
                }
                break;

            case ALARM_STATE_ARMED:
                switch (current_event) {
                    case EVENT_SHORT_PRESS:
                        ESP_LOGE(TAG, "🚨 ТРИВОГА! РУХ НА ОБ'ЄКТІ! 🚨");
                        current_state = ALARM_STATE_ALARMING;
                        break;
                    case EVENT_LONG_PRESS:
                        ESP_LOGI(TAG, "🟢 Охорону знято. DISARMED.");
                        current_state = ALARM_STATE_DISARMED;
                        break;
                    default: break;
                }
                break;

            case ALARM_STATE_ALARMING:
                switch (current_event) {
                    case EVENT_LONG_PRESS:
                        ESP_LOGI(TAG, "🟢 Тривогу скинуто. DISARMED.");
                        current_state = ALARM_STATE_DISARMED;
                        break;
                    default: break; // На коротке натискання не реагуємо!
                }
                break;
        }
    }
}

// ==========================================
// 4. ІНІЦІАЛІЗАЦІЯ
// ==========================================
void app_main(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE // <--- Переривання вимкнено!
    };
    gpio_config(&io_conf);

    xEventQueue = xQueueCreate(10, sizeof(system_event_t));
    if (xEventQueue == NULL) abort();

    // Запускаємо обидва автомати як незалежні таски
    xTaskCreate(task_button_fsm, "Btn_FSM", 2048, NULL, 5, NULL);
    xTaskCreate(task_alarm_fsm, "Alarm_FSM", 4096, NULL, 5, NULL);
}