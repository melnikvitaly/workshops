#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include "ssd1306.h"

/* ============================================================================
 *  TextRenderer — малює текст вбудованим шрифтом 5x7 через драйвер SSD1306
 * ============================================================================
 *  Використовує лише публічний API Ssd1306 (SSD1306_DrawPixel + d->width), тож
 *  драйвер лишається без знання про шрифти. Підтримує цифри 0-9, великі літери
 *  A-Z, малі 'x', 'k', 'b', 'h', 'a', а також ':', '/', '-', '.', '%' та
 *  пробіл — вистачає і на годинник з адресами I2C ("0x3C"), і на показ BME280
 *  ("T:23.4C RH:45%", "P:1013 hPa"), і на рядок лічильників ("TX120 F0 E0").
 *
 *  ⚠️ Усе, чого немає в таблиці (решта малих літер, крапка, кома...), мовчки
 *  малюється як пробіл — саме так з екрана й зникали одиниці "kb", доки для них
 *  не додали гліфи. Додаєте новий рядок — перевірте, що всі його символи тут є.
 *
 *  Приклад:
 *      Text_DrawTextCentered(&oled, 0, "12:00:00", 2);
 *      SSD1306_Flush(&oled);
 * ============================================================================ */

/* --- Геометрія вбудованого шрифту (гліф 5x7 + 1 піксель проміжку) --- */
#define TEXT_GLYPH_W     5   /* ширина гліфа в пікселях */
#define TEXT_GLYPH_H     7   /* висота гліфа в пікселях */
#define TEXT_CHAR_ADV    6   /* крок курсора: гліф 5 + проміжок 1 */

/* Розміри таблиць гліфів: рівно стільки, скільки символів у діапазонах '0'..'9'
 * та 'A'..'Z' (індексуються як c - '0' і c - 'A'). */
#define TEXT_DIGIT_COUNT 10
#define TEXT_ALPHA_COUNT 26

/* Гліфи шрифту 5x7: 5 колонок, біт row (0=верх .. 6=низ).
 * Підтримуються цифри 0-9, великі літери A-Z, малі 'x', 'k', 'b', 'h', 'a', а
 * також ':', '/', '-', '.', '%' та пробіл. Усе інше малюється як пробіл. */
static inline const uint8_t *Text_GlyphFor(char c) {
    static const uint8_t digits[TEXT_DIGIT_COUNT][TEXT_GLYPH_W] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E},  /* 0 */
        {0x00, 0x42, 0x7F, 0x40, 0x00},  /* 1 */
        {0x42, 0x61, 0x51, 0x49, 0x46},  /* 2 */
        {0x21, 0x41, 0x45, 0x4B, 0x31},  /* 3 */
        {0x18, 0x14, 0x12, 0x7F, 0x10},  /* 4 */
        {0x27, 0x45, 0x45, 0x45, 0x39},  /* 5 */
        {0x3C, 0x4A, 0x49, 0x49, 0x30},  /* 6 */
        {0x01, 0x71, 0x09, 0x05, 0x03},  /* 7 */
        {0x36, 0x49, 0x49, 0x49, 0x36},  /* 8 */
        {0x06, 0x49, 0x49, 0x29, 0x1E},  /* 9 */
    };
    /* Повна велика абетка (A-F ті самі, що були — hex-адреси не змінюються). */
    static const uint8_t alpha[TEXT_ALPHA_COUNT][TEXT_GLYPH_W] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E},  /* A */
        {0x7F, 0x49, 0x49, 0x49, 0x36},  /* B */
        {0x3E, 0x41, 0x41, 0x41, 0x22},  /* C */
        {0x7F, 0x41, 0x41, 0x22, 0x1C},  /* D */
        {0x7F, 0x49, 0x49, 0x49, 0x41},  /* E */
        {0x7F, 0x09, 0x09, 0x09, 0x01},  /* F */
        {0x3E, 0x41, 0x49, 0x49, 0x7A},  /* G */
        {0x7F, 0x08, 0x08, 0x08, 0x7F},  /* H */
        {0x00, 0x41, 0x7F, 0x41, 0x00},  /* I */
        {0x20, 0x40, 0x41, 0x3F, 0x01},  /* J */
        {0x7F, 0x08, 0x14, 0x22, 0x41},  /* K */
        {0x7F, 0x40, 0x40, 0x40, 0x40},  /* L */
        {0x7F, 0x02, 0x0C, 0x02, 0x7F},  /* M */
        {0x7F, 0x04, 0x08, 0x10, 0x7F},  /* N */
        {0x3E, 0x41, 0x41, 0x41, 0x3E},  /* O */
        {0x7F, 0x09, 0x09, 0x09, 0x06},  /* P */
        {0x3E, 0x41, 0x51, 0x21, 0x5E},  /* Q */
        {0x7F, 0x09, 0x19, 0x29, 0x46},  /* R */
        {0x46, 0x49, 0x49, 0x49, 0x31},  /* S */
        {0x01, 0x01, 0x7F, 0x01, 0x01},  /* T */
        {0x3F, 0x40, 0x40, 0x40, 0x3F},  /* U */
        {0x1F, 0x20, 0x40, 0x20, 0x1F},  /* V */
        {0x3F, 0x40, 0x38, 0x40, 0x3F},  /* W */
        {0x63, 0x14, 0x08, 0x14, 0x63},  /* X */
        {0x07, 0x08, 0x70, 0x08, 0x07},  /* Y */
        {0x61, 0x51, 0x49, 0x45, 0x43},  /* Z */
    };
    static const uint8_t lowerX[TEXT_GLYPH_W] = {0x44, 0x28, 0x10, 0x28, 0x44};  /* x */
    /* 'k' і 'b' — для одиниць "kb" (кілобіти/с) у рядку лічильників. Великими
     * писати не можна: "KB" читається як кілобайти, а темп рахується в бітах. */
    static const uint8_t lowerK[TEXT_GLYPH_W] = {0x00, 0x7F, 0x10, 0x28, 0x44};  /* k */
    static const uint8_t lowerB[TEXT_GLYPH_W] = {0x7F, 0x48, 0x44, 0x44, 0x38};  /* b */
    /* 'h' і 'a' — для одиниці тиску "hPa". Великими не можна: "HPA" не читається
     * як гектопаскалі, а саме ця одиниця стоїть у завданні. */
    static const uint8_t lowerH[TEXT_GLYPH_W] = {0x7F, 0x08, 0x04, 0x04, 0x78};  /* h */
    static const uint8_t lowerA[TEXT_GLYPH_W] = {0x20, 0x54, 0x54, 0x54, 0x78};  /* a */
    static const uint8_t colon[TEXT_GLYPH_W]  = {0x00, 0x36, 0x36, 0x00, 0x00};  /* : */
    static const uint8_t slash[TEXT_GLYPH_W]  = {0x20, 0x10, 0x08, 0x04, 0x02};  /* / */
    static const uint8_t dash[TEXT_GLYPH_W]   = {0x08, 0x08, 0x08, 0x08, 0x08};  /* - */
    /* '.' — десяткова кома температури ("23.4"), '%' — вологість і світло.
     * Без них рядки BME280 виглядали б як "T:234C" і "RH:45": саме так з екрана
     * і зникають символи, яких немає в таблиці (див. попередження вище). */
    static const uint8_t dot[TEXT_GLYPH_W]    = {0x00, 0x60, 0x60, 0x00, 0x00};  /* . */
    static const uint8_t percent[TEXT_GLYPH_W]= {0x23, 0x13, 0x08, 0x64, 0x62};  /* % */
    static const uint8_t blank[TEXT_GLYPH_W]  = {0x00, 0x00, 0x00, 0x00, 0x00};  /* space */

    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return alpha[c - 'A'];
    if (c == 'x')             return lowerX;
    if (c == 'k')             return lowerK;
    if (c == 'b')             return lowerB;
    if (c == 'h')             return lowerH;
    if (c == 'a')             return lowerA;
    if (c == ':')             return colon;
    if (c == '/')             return slash;
    if (c == '-')             return dash;
    if (c == '.')             return dot;
    if (c == '%')             return percent;
    return blank;
}

/* Малює один символ у буфер. scale збільшує розмір (1 = 5x7, 2 = 10x14...).
 * Повертає крок курсора в пікселях (для зсуву на наступний символ). */
static inline int16_t Text_DrawChar(Ssd1306 *d, int16_t x, int16_t y, char c, uint8_t scale) {
    const uint8_t *glyph = Text_GlyphFor(c);
    for (uint8_t col = 0; col < TEXT_GLYPH_W; ++col) {
        const uint8_t bits = glyph[col];
        for (uint8_t row = 0; row < TEXT_GLYPH_H; ++row) {
            if (bits & (1 << row)) {
                /* масштабуємо піксель у квадрат scale x scale */
                for (uint8_t sx = 0; sx < scale; ++sx) {
                    for (uint8_t sy = 0; sy < scale; ++sy) {
                        SSD1306_DrawPixel(d, x + col * scale + sx, y + row * scale + sy, 1);
                    }
                }
            }
        }
    }
    return (int16_t)(TEXT_CHAR_ADV * scale);  /* гліф + проміжок */
}

/* Малює рядок у буфер, починаючи з (x, y). Повертає кінцеву X-координату. */
static inline int16_t Text_DrawText(Ssd1306 *d, int16_t x, int16_t y, const char *str, uint8_t scale) {
    for (const char *p = str; *p; ++p) {
        x += Text_DrawChar(d, x, y, *p, scale);
    }
    return x;
}

/* Ширина рядка в пікселях (для центрування) */
static inline int16_t Text_Width(const char *str, uint8_t scale) {
    int16_t n = 0;
    for (const char *p = str; *p; ++p) ++n;
    return (int16_t)(n * TEXT_CHAR_ADV * scale);
}

/* Малює рядок по центру екрана по горизонталі на висоті y.
 * Повертає кінцеву X-координату (як Text_DrawText). */
static inline int16_t Text_DrawTextCentered(Ssd1306 *d, int16_t y, const char *str, uint8_t scale) {
    int16_t x = (int16_t)((d->width - Text_Width(str, scale)) / 2);
    return Text_DrawText(d, x, y, str, scale);
}

#endif /* TEXT_RENDERER_H */
