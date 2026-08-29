#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "mqtt_client.h"

// ================= НАЛАШТУВАННЯ (Сучасний C++) =================
constexpr const char *WIFI_SSID = "YOUR_WIFI_SSID";
// Вписати SSID мережі в аудиторії
constexpr const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";
// Вписати пароль
constexpr const char *BROKER_URI = "mqtt://broker.hivemq.com";

// Студент має вписати тут своє ім'я або нікнейм (без пробілів)
constexpr const char *STUDENT_NAME = "oivanchuk";
// ===============================================================

static const char *TAG = "MQTT_LESSON";

// Прапорець для захисту від повторного запуску MQTT при реконектах Wi-Fi
static bool is_mqtt_started = false;

// Попереднє оголошення функції старту MQTT
static void mqtt_app_start(void);

/*
 * 1. ОБРОБНИК ПОДІЙ WI-FI ТА IP
 * Асинхронна реакція на стан мережі.
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "Втрачено зв'язок з Wi-Fi. Перепідключення...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Отримано IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // ТІЛЬКИ ПІСЛЯ ОТРИМАННЯ IP МОЖНА ЗАПУСКАТИ MQTT
        if (!is_mqtt_started)
        {
            mqtt_app_start();
            is_mqtt_started = true;
        }
        else
        {
            // Драйвер MQTT в ESP-IDF має власний механізм реконекту,
            // тому просто чекаємо, поки він сам відновить з'єднання.
            ESP_LOGI(TAG, "Wi-Fi відновлено, MQTT перепідключиться автоматично");
        }
    }
}

/*
 * 2. ОБРОБНИК ПОДІЙ MQTT
 * Читання та публікація повідомлень.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;

    // Безпечне виділення пам'яті виключно на стеку (жодного Heap Fragmentation!)
    char topic_name[64];
    char msg_data[64];

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Підключено до брокера!");

        // 1. Публікація привітання (Використовуємо snprintf для жорсткого контролю меж)
        snprintf(msg_data, sizeof(msg_data), "Hello from %s!", STUDENT_NAME);
        esp_mqtt_client_publish(client, "EMB1/hello", msg_data, 0, 1, 0);
        ESP_LOGI(TAG, "Опубліковано: %s", msg_data);

        // 2. Підписка на свій власний топік для команд
        snprintf(topic_name, sizeof(topic_name), "EMB1/%s/cmd", STUDENT_NAME);
        esp_mqtt_client_subscribe(client, topic_name, 0);
        ESP_LOGI(TAG, "Підписано на топік: %s", topic_name);

        // 3. Підписка на топік викладача (для тесту)
        esp_mqtt_client_subscribe(client, "EMB1/teacher/#", 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT Відключено");
        break;

    case MQTT_EVENT_DATA:
        // event->topic та event->data НЕ мають нуль-термінатора ('\0').
        // %.*s змушує printf читати рівно ту кількість байтів, яка вказана в _len.
        ESP_LOGI(TAG, "--- ОТРИМАНО НОВЕ ПОВІДОМЛЕННЯ ---");
        printf("ТОПІК: %.*s\r\n", event->topic_len, event->topic);
        printf("ДАНІ: %.*s\r\n", event->data_len, event->data);
        printf("------------------------------------\n");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Помилка");
        break;

    default:
        break;
    }
}

/*
 * 3. ІНІЦІАЛІЗАЦІЯ MQTT
 */
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = BROKER_URI;

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

/*
 * 4. ІНІЦІАЛІЗАЦІЯ WI-FI
 */
void wifi_init_sta(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Реєстрація обробника подій
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, MQTT_EVENT_ANY, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {};
    // Використовуємо strncpy для безпечного копіювання constexpr рядків у масиви структури
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/*
 * 5. ГОЛОВНА ФУНКЦІЯ (app_main має залишатися з C-лінковкою для запуску системою)
 */
extern "C" void app_main(void)
{
    // Ініціалізація енергонезалежної пам'яті (NVS) для Wi-Fi стеку
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Запуск ESP32-S3... Налаштування Wi-Fi");
    wifi_init_sta();

    // Далі головний таск може заснути або виконувати іншу корисну роботу (напр. моніторинг датчиків),
    // оскільки Wi-Fi та MQTT працюють у власних фонових тасках (асинхронно).
}