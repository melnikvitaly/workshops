#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>



#define RTC_I2C_SDA 4
#define RTC_I2C_SCL 5



#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2C_LESSON";

// Налаштування пінів (для ESP32-S3 можна обирати майже будь-які вільні GPIO)
#define I2C_MASTER_SCL_IO           3
#define I2C_MASTER_SDA_IO           4
#define I2C_MASTER_NUM              (i2c_port_t)0      // Номер порту I2C
#define I2C_MASTER_FREQ_HZ          100000 // Стандартна частота 100 кГц
#define RTC_ADDR                    0x68   // I2C адреса DS1307

// Функція для ініціалізації шини I2C
esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    conf.clk_flags = 0; // Використовуємо стандартний джерело тактового сигналу

    i2c_param_config( I2C_MASTER_NUM, &conf);
    // Встановлюємо драйвер без буферів (бо ми Master)
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// Функція конвертації з BCD формату у звичайний десятковий
uint8_t bcd_to_dec(uint8_t val) {
    return (val >> 4) * 10 + (val & 0x0F);
}

// Функція для зчитування секунд з RTC
void read_rtc_seconds() {
    uint8_t data_rx; // Змінна для збереження зчитаних даних

    // 1. Створюємо "ланцюжок" команд для I2C
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    // 2. Формуємо запит на запис: вказуємо адресу пристрою і номер регістра (0x00 - секунди)
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (RTC_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true); // Регістр секунд

    // 3. Робимо Restart і просимо пристрій віддати дані
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (RTC_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data_rx, I2C_MASTER_NACK); // Читаємо 1 байт і кажемо NACK (кінець)
    i2c_master_stop(cmd);

    // 4. Виконуємо сформований ланцюжок команд
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd); // Очищуємо пам'ять

    if (ret == ESP_OK) {
        // Конвертуємо BCD формат у звичайні цифри
        uint8_t seconds = bcd_to_dec(data_rx & 0x7F); // 0x7F відкидає старший біт CH (Clock Halt)
        ESP_LOGI(TAG, "Поточні секунди: %02d", seconds);
    } else {
        ESP_LOGE(TAG, "Помилка читання I2C: %s", esp_err_to_name(ret));
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Ініціалізація I2C...");
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C ініціалізовано успішно!");

    while (1) {
        read_rtc_seconds();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Чекаємо 1 секунду
    }
}