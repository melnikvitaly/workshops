#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ds1307.hpp"
#include "ssd1306.hpp"
#include "i2c_scanner.hpp"
#include "cat.hpp"

static const char *TAG = "I2C_LESSON";

// Налаштування пінів (для ESP32-S3 можна обирати майже будь-які вільні GPIO)
#define I2C_MASTER_SCL_IO           3
#define I2C_MASTER_SDA_IO           4
#define I2C_MASTER_NUM              (i2c_port_t)0      // Номер порту I2C
#define I2C_MASTER_FREQ_HZ          400000             // 400 кГц (швидше для OLED)

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

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Ініціалізація I2C...");
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C ініціалізовано успішно!");

    I2cScanner scanner(I2C_MASTER_NUM);  // скануємо в циклі кожні 10 с

    // Створюємо об'єкти драйверів на спільній шині I2C
    Ds1307  rtc(I2C_MASTER_NUM);                  // RTC за адресою 0x68
    Ssd1306 oled(I2C_MASTER_NUM, 0x3C, 128, 64);  // OLED за адресою 0x3C
    Cat     cat(oled);
    bool    oled_ok = false;

    if (oled.init() == ESP_OK) {
        ESP_LOGI(TAG, "OLED ініціалізовано");
        oled_ok = true;
    } else {
        ESP_LOGE(TAG, "OLED не знайдено");
    }

    // Розташування кота нижче, щоб зверху помістилися годинник + адреси
    const int16_t cat_cx = oled.width() / 2;
    const int16_t cat_cy = 46;
    const int16_t cat_r  = 14;
    int16_t ear_tilt = 0;  // чергуємо кожну секунду -> вуха ворушаться

    // Періодичне сканування — за реальним часом, а не за лічильником ітерацій
    // (робота в циклі займає час, тож ітерація != рівно 1 с).
    const int64_t SCAN_PERIOD_US = 10 * 1000000LL;  // 10 секунд
    int64_t last_scan_us = -SCAN_PERIOD_US;          // щоб сканувати одразу

    // Останній результат сканування (показуємо на екрані щокадру).
    // Шрифт має лише цифри/A-F/x/':'/пробіл, тож стан показуємо у hex.
    char devices[32] = "0x00";  // плейсхолдер до першого скану

    while (1) {
        // Скануємо шину, якщо від попереднього скану минуло >= 10 с
        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_scan_us >= SCAN_PERIOD_US) {
            uint8_t addrs[8];
            const int n = scanner.scan(addrs, 8);
            last_scan_us = now_us;

            // Формуємо рядок "0x3C 0x68 ..." (стільки, скільки влізе)
            int pos = 0;
            for (int i = 0; i < n && i < 4; ++i) {
                pos += snprintf(devices + pos, sizeof(devices) - pos,
                                "%s0x%02X", (i ? " " : ""), addrs[i]);
            }
            if (n == 0) {
                snprintf(devices, sizeof(devices), "0x00");  // нічого не знайдено
            }
        }

        RtcTime t = {};
        char clock[16] = "--:--:--";
        if (rtc.readTime(t) == ESP_OK) {
            snprintf(clock, sizeof(clock), "%02d:%02d:%02d",
                     t.hours, t.minutes, t.seconds);
            ESP_LOGI(TAG, "Час: %s", clock);
        }

        if (oled_ok) {
            // Кадр: кіт (без flush) + годинник зверху по центру, потім flush
            ear_tilt = (ear_tilt == 0) ? 5 : 0;  // проста анімація вух
            cat.draw(cat_cx, cat_cy, cat_r, ear_tilt, /*flush=*/false);

            const uint8_t scale = 2;  // 10x14 пікселів на символ
            const int16_t tx = (oled.width() - Ssd1306::textWidth(clock, scale)) / 2;
            oled.drawText(tx, 0, clock, scale);  // годинник у верхньому рядку

            // Адреси знайдених I2C-пристроїв (дрібним шрифтом під годинником)
            const int16_t dx = (oled.width() - Ssd1306::textWidth(devices, 1)) / 2;
            oled.drawText(dx, 16, devices, 1);
            oled.flush();
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Чекаємо 1 секунду
    }
}
