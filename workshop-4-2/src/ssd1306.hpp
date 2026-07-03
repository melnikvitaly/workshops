#pragma once

#include <cstdint>
#include <cstring>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================================
//  SSD1306 OLED driver (I2C) — header-only
// ============================================================================
//
//  ПЛАН ІНІЦІАЛІЗАЦІЇ (послідовність команд -> на адресу 0x3C, control byte 0x00)
//  --------------------------------------------------------------------------
//   1. 0xAE                 Display OFF
//   2. 0xA8, 0x1F/0x3F      Set multiplex ratio (0x1F=32 рядки, 0x3F=64 рядки)
//   3. 0xD3, 0x00           Set display offset = 0
//   4. 0x40                 Set start line = 0  (0x40 | line)
//   5. 0x20, 0x00           Memory addressing mode = Horizontal
//   6. 0xA1                 Segment remap (0xA0 норм, 0xA1 = дзеркало по X)
//   7. 0xC8                 COM scan direction (0xC0 норм, 0xC8 = дзеркало по Y)
//   8. 0xDA, 0x02/0x12      COM pins config (0x02 для 32, 0x12 для 64)
//   9. 0x81, 0x7F           Set contrast
//  10. 0xD9, 0xF1           Set pre-charge period
//  11. 0xDB, 0x40           Set VCOMH deselect level
//  12. 0xA4                 Resume to RAM content (0xA5 = всі пікселі ON, тест)
//  13. 0xA6                 Normal display (0xA7 = інверсія)
//  14. 0x8D, 0x14           Charge pump ON (обов'язково для внутр. живлення)
//  15. 0xAF                 Display ON
//
//  Дані пікселів відправляються з control byte 0x40.
//  Кадр = width * height / 8 байт (наприклад 128*64/8 = 1024 байти).
// ============================================================================

class Ssd1306 {
public:
    static constexpr uint8_t CTRL_CMD  = 0x00;  // далі йдуть команди
    static constexpr uint8_t CTRL_DATA = 0x40;  // далі йдуть дані (GDDRAM)

    // width/height типово 128x64 або 128x32; address типово 0x3C (або 0x3D)
    Ssd1306(i2c_port_t port, uint8_t address = 0x3C,
            uint8_t width = 128, uint8_t height = 64)
        : port_(port), address_(address), width_(width), height_(height) {
        bufferLen_ = static_cast<size_t>(width_) * height_ / 8;
        if (bufferLen_ > kMaxBuffer) {
            bufferLen_ = kMaxBuffer;
        }
        clear();
    }

    // Повний план ініціалізації. Шину I2C (i2c_driver_install) треба
    // ініціалізувати ЗОВНІ до виклику init().
    esp_err_t init() {
        // Значення, що залежать від висоти панелі (32 vs 64 рядки)
        const uint8_t multiplex = (height_ == 32) ? 0x1F : 0x3F;  // п.2
        const uint8_t comPins   = (height_ == 32) ? 0x02 : 0x12;  // п.8

        const uint8_t seq[] = {
            0xAE,                  // 1. Display OFF
            0xA8, multiplex,       // 2. Set multiplex ratio
            0xD3, 0x00,            // 3. Set display offset = 0
            0x40,                  // 4. Set start line = 0
            0x20, 0x00,            // 5. Addressing mode = Horizontal
            0xA1,                  // 6. Segment remap (дзеркало по X)
            0xC8,                  // 7. COM scan direction (дзеркало по Y)
            0xDA, comPins,         // 8. COM pins config
            0x81, 0x7F,            // 9. Contrast
            0xD9, 0xF1,            // 10. Pre-charge period
            0xDB, 0x40,            // 11. VCOMH deselect level
            0xA4,                  // 12. Resume to RAM content
            0xA6,                  // 13. Normal (не інверсний) режим
            0x8D, 0x14,            // 14. Charge pump ON
            0xAF,                  // 15. Display ON
        };

        esp_err_t ret = commands(seq, sizeof(seq));
        if (ret != ESP_OK) {
            ESP_LOGE(kTag, "init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  // даємо charge pump стабілізуватись
        clear();
        return flush();
    }

    // --- Низькорівневі команди ---
    esp_err_t command(uint8_t cmd) {
        i2c_cmd_handle_t link = i2c_cmd_link_create();
        i2c_master_start(link);
        i2c_master_write_byte(link, (address_ << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(link, CTRL_CMD, true);
        i2c_master_write_byte(link, cmd, true);
        i2c_master_stop(link);
        esp_err_t ret = i2c_master_cmd_begin(port_, link, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(link);
        return ret;
    }

    esp_err_t command(uint8_t cmd, uint8_t arg) {
        const uint8_t pair[2] = {cmd, arg};
        return commands(pair, 2);
    }

    esp_err_t commands(const uint8_t *cmds, size_t len) {
        i2c_cmd_handle_t link = i2c_cmd_link_create();
        i2c_master_start(link);
        i2c_master_write_byte(link, (address_ << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(link, CTRL_CMD, true);
        i2c_master_write(link, const_cast<uint8_t *>(cmds), len, true);
        i2c_master_stop(link);
        esp_err_t ret = i2c_master_cmd_begin(port_, link, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(link);
        return ret;
    }

    // --- Робота з кадровим буфером ---
    void clear(uint8_t pattern = 0x00) {
        memset(buffer_, pattern, bufferLen_);
    }

    void drawPixel(int16_t x, int16_t y, bool on = true) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return;
        }
        // Сторінкова упаковка: байт містить 8 пікселів по вертикалі.
        const size_t index = static_cast<size_t>(x) + (y / 8) * width_;
        const uint8_t bit = 1 << (y & 0x07);
        if (on) {
            buffer_[index] |= bit;
        } else {
            buffer_[index] &= ~bit;
        }
    }

    esp_err_t flush() {
        // Виставляємо вікно запису на весь екран (horizontal addressing mode)
        const uint8_t window[] = {
            0x21, 0x00, static_cast<uint8_t>(width_ - 1),       // column 0..W-1
            0x22, 0x00, static_cast<uint8_t>(height_ / 8 - 1),  // page 0..pages-1
        };
        esp_err_t ret = commands(window, sizeof(window));
        if (ret != ESP_OK) {
            return ret;
        }
        return sendData(buffer_, bufferLen_);
    }

    // --- Зручні налаштування ---
    esp_err_t setContrast(uint8_t value) { return command(0x81, value); }
    esp_err_t displayOn()                { return command(0xAF); }
    esp_err_t displayOff()               { return command(0xAE); }
    esp_err_t invert(bool on)            { return command(on ? 0xA7 : 0xA6); }

    // --- Текст (вбудований шрифт 5x7: цифри, ':', ' ') ---
    // Малює один символ у буфер. scale збільшує розмір (1 = 5x7, 2 = 10x14...).
    // Повертає ширину намальованого символа в пікселях (для зсуву курсора).
    int16_t drawChar(int16_t x, int16_t y, char c, uint8_t scale = 1) {
        const uint8_t *glyph = glyphFor(c);
        for (uint8_t col = 0; col < 5; ++col) {
            const uint8_t bits = glyph[col];
            for (uint8_t row = 0; row < 7; ++row) {
                if (bits & (1 << row)) {
                    // масштабуємо піксель у квадрат scale x scale
                    for (uint8_t sx = 0; sx < scale; ++sx) {
                        for (uint8_t sy = 0; sy < scale; ++sy) {
                            drawPixel(x + col * scale + sx, y + row * scale + sy, true);
                        }
                    }
                }
            }
        }
        return 6 * scale;  // 5 пікселів гліф + 1 пробіл
    }

    // Малює рядок у буфер, починаючи з (x, y). Повертає кінцеву X-координату.
    int16_t drawText(int16_t x, int16_t y, const char *str, uint8_t scale = 1) {
        for (const char *p = str; *p; ++p) {
            x += drawChar(x, y, *p, scale);
        }
        return x;
    }

    // Ширина рядка в пікселях (для центрування)
    static int16_t textWidth(const char *str, uint8_t scale = 1) {
        int16_t n = 0;
        for (const char *p = str; *p; ++p) ++n;
        return n * 6 * scale;
    }

    uint8_t width()  const { return width_; }
    uint8_t height() const { return height_; }

private:
    // Передати масив байтів даних (control byte 0x40) на дисплей
    esp_err_t sendData(const uint8_t *data, size_t len) {
        i2c_cmd_handle_t link = i2c_cmd_link_create();
        i2c_master_start(link);
        i2c_master_write_byte(link, (address_ << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(link, CTRL_DATA, true);
        i2c_master_write(link, const_cast<uint8_t *>(data), len, true);
        i2c_master_stop(link);
        esp_err_t ret = i2c_master_cmd_begin(port_, link, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(link);
        return ret;
    }

    // Гліфи шрифту 5x7: 5 колонок, біт row (0=верх .. 6=низ).
    // Підтримуються цифри 0-9, hex-літери A-F, 'x', ':' та пробіл
    // (достатньо для годинника та адрес I2C на кшталт "0x3C").
    static const uint8_t *glyphFor(char c) {
        static const uint8_t digits[10][5] = {
            {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
            {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
            {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
            {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
            {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
            {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
            {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
            {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
            {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
            {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
        };
        static const uint8_t hexAF[6][5] = {
            {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
            {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
            {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
            {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
            {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
            {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
        };
        static const uint8_t lowerX[5] = {0x44, 0x28, 0x10, 0x28, 0x44};  // x
        static const uint8_t colon[5]  = {0x00, 0x36, 0x36, 0x00, 0x00};  // :
        static const uint8_t blank[5]  = {0x00, 0x00, 0x00, 0x00, 0x00};  // space

        if (c >= '0' && c <= '9') return digits[c - '0'];
        if (c >= 'A' && c <= 'F') return hexAF[c - 'A'];
        if (c == 'x')             return lowerX;
        if (c == ':')             return colon;
        return blank;
    }

    static constexpr const char *kTag = "SSD1306";

    i2c_port_t port_;
    uint8_t    address_;
    uint8_t    width_;
    uint8_t    height_;

    // Кадровий буфер: 1 біт = 1 піксель, упаковка по сторінках (8 рядків/байт).
    static constexpr size_t kMaxBuffer = 128 * 64 / 8;  // максимум для 128x64
    uint8_t buffer_[kMaxBuffer];
    size_t  bufferLen_;
};
