#pragma once

#include <cstdint>
#include "ssd1306.hpp"

// ============================================================================
//  TextRenderer — малює текст вбудованим шрифтом 5x7 через драйвер SSD1306
// ============================================================================
//  Використовує лише публічний API Ssd1306 (drawPixel + width), тож драйвер
//  лишається без знання про шрифти. Підтримує цифри 0-9, hex-літери A-F,
//  'x', ':' та пробіл (достатньо для годинника й адрес I2C на кшталт "0x3C").
//
//  Приклад:
//      Ssd1306 oled(I2C_NUM_0);
//      TextRenderer text(oled);
//      text.drawTextCentered(0, "12:00:00", 2);
//      oled.flush();
// ============================================================================

class TextRenderer {
public:
    // --- Геометрія вбудованого шрифту (гліф 5x7 + 1 піксель проміжку) ---
    static constexpr int16_t kGlyphWidth  = 5;  // ширина гліфа в пікселях
    static constexpr int16_t kGlyphHeight = 7;  // висота гліфа в пікселях
    static constexpr int16_t kCharSpacing = 1;  // проміжок між символами
    static constexpr int16_t kCharAdvance = kGlyphWidth + kCharSpacing;  // крок курсора

    explicit TextRenderer(Ssd1306 &oled) : oled_(oled) {}

    // Малює один символ у буфер. scale збільшує розмір (1 = 5x7, 2 = 10x14...).
    // Повертає крок курсора в пікселях (для зсуву на наступний символ).
    int16_t drawChar(int16_t x, int16_t y, char c, uint8_t scale = 1) {
        const uint8_t *glyph = glyphFor(c);
        for (uint8_t col = 0; col < kGlyphWidth; ++col) {
            const uint8_t bits = glyph[col];
            for (uint8_t row = 0; row < kGlyphHeight; ++row) {
                if (bits & (1 << row)) {
                    // масштабуємо піксель у квадрат scale x scale
                    for (uint8_t sx = 0; sx < scale; ++sx) {
                        for (uint8_t sy = 0; sy < scale; ++sy) {
                            oled_.drawPixel(x + col * scale + sx, y + row * scale + sy, true);
                        }
                    }
                }
            }
        }
        return kCharAdvance * scale;  // гліф + проміжок
    }

    // Малює рядок у буфер, починаючи з (x, y). Повертає кінцеву X-координату.
    int16_t drawText(int16_t x, int16_t y, const char *str, uint8_t scale = 1) {
        for (const char *p = str; *p; ++p) {
            x += drawChar(x, y, *p, scale);
        }
        return x;
    }

    // Малює рядок по центру екрана по горизонталі на висоті y.
    // Повертає кінцеву X-координату (як drawText).
    int16_t drawTextCentered(int16_t y, const char *str, uint8_t scale = 1) {
        const int16_t x = (oled_.width() - textWidth(str, scale)) / 2;
        return drawText(x, y, str, scale);
    }

    // Ширина рядка в пікселях (для центрування)
    static int16_t textWidth(const char *str, uint8_t scale = 1) {
        int16_t n = 0;
        for (const char *p = str; *p; ++p) ++n;
        return n * kCharAdvance * scale;
    }

private:
    // Гліфи шрифту 5x7: 5 колонок, біт row (0=верх .. 6=низ).
    // Підтримуються цифри 0-9, hex-літери A-F, 'x', ':' та пробіл
    // (достатньо для годинника та адрес I2C на кшталт "0x3C").
    static const uint8_t *glyphFor(char c) {
        static const uint8_t digits[10][kGlyphWidth] = {
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
        static const uint8_t hexAF[6][kGlyphWidth] = {
            {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
            {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
            {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
            {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
            {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
            {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
        };
        static const uint8_t lowerX[kGlyphWidth] = {0x44, 0x28, 0x10, 0x28, 0x44};  // x
        static const uint8_t colon[kGlyphWidth]  = {0x00, 0x36, 0x36, 0x00, 0x00};  // :
        static const uint8_t blank[kGlyphWidth]  = {0x00, 0x00, 0x00, 0x00, 0x00};  // space

        if (c >= '0' && c <= '9') return digits[c - '0'];
        if (c >= 'A' && c <= 'F') return hexAF[c - 'A'];
        if (c == 'x')             return lowerX;
        if (c == ':')             return colon;
        return blank;
    }

    Ssd1306 &oled_;
};
