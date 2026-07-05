#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.hpp"
#include "ds1307.hpp"
#include "ssd1306.hpp"
#include "text_renderer.hpp"
#include "i2c_scanner.hpp"
#include "cat.hpp"

using namespace config;  // усі налаштування — у config.hpp

static const char *TAG = "I2C_LESSON";

I2cScanner   scanner(I2C_MASTER_NUM);  // скануємо в циклі кожні 10 с
Ds1307       rtc(I2C_MASTER_NUM);      // RTC за адресою 0x68
Ssd1306      oled(I2C_MASTER_NUM, OLED_ADDR, OLED_WIDTH, OLED_HEIGHT);  // OLED
Cat          cat(oled);
TextRenderer text(oled);  // малює годинник/адреси шрифтом 5x7  

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

    i2c_param_config(I2C_MASTER_NUM, &conf);
    // Встановлюємо драйвер без буферів (бо ми Master)
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// Рядки одного кадру (показуємо на екрані щокадру). Шрифт має лише
// цифри/A-F/x/':'/пробіл, тож стан I2C-шини показуємо у hex.
struct FrameData {
    char devices[DEVICES_STR_LEN];  // "0x3C 0x68 ..." — результат сканування
    char clock[CLOCK_STR_LEN];      // "HH:MM:SS" — час з RTC
};

// Просканувати шину не частіше, ніж раз на SCAN_PERIOD_US, і оновити рядок
// frame.devices. За реальним часом, а не за лічильником ітерацій (робота в
// циклі займає час, тож ітерація != рівно 1 с).
static void poll_i2c_scan(FrameData &frame, int64_t &last_scan_us) {
    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_scan_us >= SCAN_PERIOD_US) {
        scanner.scanToString(frame.devices, sizeof(frame.devices), MAX_DEVICES_SHOWN);
        last_scan_us = now_us;
    }
}

// Намалювати один кадр: кіт + годинник зверху + адреси знайдених пристроїв.
static void render_frame(const FrameData &frame) {
    // Нахил вух чергується щокадру -> вуха ворушаться (стан між викликами).
    static int16_t ear_tilt = 0;
    ear_tilt = (ear_tilt == 0) ? EAR_TILT_MAX : 0;  // проста анімація вух
    cat.draw(oled.width() / 2, CAT_CENTER_Y, CAT_RADIUS, ear_tilt, /*flush=*/false);

    text.drawTextCentered(CLOCK_Y, frame.clock, CLOCK_SCALE);        // годинник зверху
    text.drawTextCentered(DEVICES_Y, frame.devices, DEVICES_SCALE);  // адреси під ним
    oled.flush();
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Ініціалізація I2C...");
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C ініціалізовано успішно!");

    // Драйвери/помічники (scanner, rtc, oled, cat, text) оголошені у файловій
    // області вище. Тут лишається лише ініціалізація OLED та головний цикл.
    // Готовність дисплея тепер зберігає сам драйвер (oled.ready()).
    if (oled.init() == ESP_OK) {
        ESP_LOGI(TAG, "OLED ініціалізовано");
    } else {
        ESP_LOGE(TAG, "OLED не знайдено");
    }

    // Періодичне сканування — за реальним часом (див. poll_i2c_scan).
    int64_t last_scan_us = -SCAN_PERIOD_US;  // щоб сканувати одразу

    // Дані кадру. Заповнюються до першого показу: devices — у poll_i2c_scan
    // (перший скан спрацьовує одразу), clock — з RTC.
    FrameData frame;

    while (1) {
        poll_i2c_scan(frame, last_scan_us);

        if (rtc.readTimeString(frame.clock, sizeof(frame.clock)) == ESP_OK) {
            ESP_LOGI(TAG, "Час: %s", frame.clock);
        }

        if (oled.ready()) {
            render_frame(frame);
        }

        vTaskDelay(LOOP_PERIOD_MS / portTICK_PERIOD_MS);  // Чекаємо 1 секунду
    }
}
