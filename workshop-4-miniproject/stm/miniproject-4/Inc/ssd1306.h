#ifndef SSD1306_H
#define SSD1306_H

#include <string.h>
#include "stm32f4xx_hal.h"

/* ============================================================================
 *  SSD1306 OLED driver (I2C) — header-only, STM32 HAL
 * ============================================================================
 *
 *  ПЛАН ІНІЦІАЛІЗАЦІЇ (послідовність команд -> control byte 0x00)
 *  --------------------------------------------------------------------------
 *   1. 0xAE                 Display OFF
 *   2. 0xA8, 0x1F/0x3F      Set multiplex ratio (0x1F=32 рядки, 0x3F=64 рядки)
 *   3. 0xD3, 0x00           Set display offset = 0
 *   4. 0x40                 Set start line = 0
 *   5. 0x20, 0x00           Memory addressing mode = Horizontal
 *   6. 0xA1                 Segment remap (дзеркало по X)
 *   7. 0xC8                 COM scan direction (дзеркало по Y)
 *   8. 0xDA, 0x02/0x12      COM pins config (0x02 для 32, 0x12 для 64)
 *   9. 0x81, 0x7F           Set contrast
 *  10. 0xD9, 0xF1           Set pre-charge period
 *  11. 0xDB, 0x40           Set VCOMH deselect level
 *  12. 0xA4                 Resume to RAM content
 *  13. 0xA6                 Normal display (0xA7 = інверсія)
 *  14. 0x8D, 0x14           Charge pump ON (обов'язково для внутр. живлення)
 *  15. 0xAF                 Display ON
 *
 *  Команди відправляються через control byte 0x00, дані пікселів — 0x40.
 *  У HAL це зручно робити через HAL_I2C_Mem_Write: memaddr = control byte.
 *  Кадр = width * height / 8 байт (наприклад 128*64/8 = 1024 байти).
 * ============================================================================ */

#define SSD1306_CTRL_CMD   0x00   /* далі йдуть команди */
#define SSD1306_CTRL_DATA  0x40   /* далі йдуть дані (GDDRAM) */
#define SSD1306_MAX_BUFFER (128 * 64 / 8)

/* Таймаут одного I2C-обміну (мс). Це дедлайн на ВЕСЬ обмін, а не на байт:
 * HAL_I2C_Mem_Write бере tickstart один раз і звіряє з ним кожне очікування
 * прапорця. Кадр 128x64 = 1024 байти даних + control byte, на 100 кГц це ~9 біт
 * на байт => ~92 мс. Тобто попередні 100 мс лишали менше 8% запасу, а зрив по
 * таймауту тут фатальний: HAL виставляє STOP лише при NACK, тож після таймауту
 * шина лишається BUSY і всі наступні виклики одразу повертають HAL_BUSY —
 * картинка застигає назавжди. Беремо запас на порядок. */
#define SSD1306_I2C_TIMEOUT 1000

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t            addr;       /* зсунута 8-бітна адреса для HAL */
    uint8_t            width;
    uint8_t            height;
    uint8_t            ready;      /* 1 після успішної SSD1306_Init */
    uint16_t           bufferLen;
    /* Кадровий буфер: 1 біт = 1 піксель, упаковка по сторінках (8 рядків/байт). */
    uint8_t            buffer[SSD1306_MAX_BUFFER];
} Ssd1306;

/* --- Низькорівневий обмін --- */
static inline HAL_StatusTypeDef SSD1306_Commands(Ssd1306 *d, const uint8_t *cmds, uint16_t len) {
    return HAL_I2C_Mem_Write(d->hi2c, d->addr, SSD1306_CTRL_CMD, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)cmds, len, SSD1306_I2C_TIMEOUT);
}

static inline HAL_StatusTypeDef SSD1306_Command(Ssd1306 *d, uint8_t cmd) {
    return SSD1306_Commands(d, &cmd, 1);
}

static inline HAL_StatusTypeDef SSD1306_SendData(Ssd1306 *d, const uint8_t *data, uint16_t len) {
    return HAL_I2C_Mem_Write(d->hi2c, d->addr, SSD1306_CTRL_DATA, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)data, len, SSD1306_I2C_TIMEOUT);
}

/* --- Робота з кадровим буфером --- */
static inline void SSD1306_Clear(Ssd1306 *d, uint8_t pattern) {
    memset(d->buffer, pattern, d->bufferLen);
}

static inline void SSD1306_DrawPixel(Ssd1306 *d, int16_t x, int16_t y, uint8_t on) {
    if (x < 0 || y < 0 || x >= d->width || y >= d->height) {
        return;
    }
    /* Сторінкова упаковка: байт містить 8 пікселів по вертикалі. */
    uint16_t index = (uint16_t)x + (uint16_t)(y / 8) * d->width;
    uint8_t  bit   = (uint8_t)(1 << (y & 0x07));
    if (on) {
        d->buffer[index] |= bit;
    } else {
        d->buffer[index] &= (uint8_t)~bit;
    }
}

/* Виставляємо вікно запису на весь екран і відправляємо буфер. */
static inline HAL_StatusTypeDef SSD1306_Flush(Ssd1306 *d) {
    const uint8_t window[] = {
        0x21, 0x00, (uint8_t)(d->width - 1),       /* column 0..W-1 */
        0x22, 0x00, (uint8_t)(d->height / 8 - 1),  /* page 0..pages-1 */
    };
    HAL_StatusTypeDef ret = SSD1306_Commands(d, window, sizeof(window));
    if (ret != HAL_OK) {
        return ret;
    }
    return SSD1306_SendData(d, d->buffer, d->bufferLen);
}

/* Заповнити структуру перед SSD1306_Init (address типово 0x3C, 128x64 або 128x32) */
static inline void SSD1306_Setup(Ssd1306 *d, I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                 uint8_t width, uint8_t height) {
    d->hi2c   = hi2c;
    d->addr   = (uint8_t)(addr7 << 1);
    d->width  = width;
    d->height = height;
    d->ready  = 0;  /* готовність виставляє SSD1306_Init */
    d->bufferLen = (uint16_t)((uint16_t)width * height / 8);
    if (d->bufferLen > SSD1306_MAX_BUFFER) {
        d->bufferLen = SSD1306_MAX_BUFFER;
    }
    SSD1306_Clear(d, 0x00);
}

/* Повний план ініціалізації. I2C має бути ініціалізовано ЗОВНІ до виклику. */
static inline HAL_StatusTypeDef SSD1306_Init(Ssd1306 *d) {
    const uint8_t multiplex = (d->height == 32) ? 0x1F : 0x3F;  /* п.2 */
    const uint8_t comPins   = (d->height == 32) ? 0x02 : 0x12;  /* п.8 */
    const uint8_t seq[] = {
        0xAE,                  /* 1. Display OFF */
        0xA8, multiplex,       /* 2. Set multiplex ratio */
        0xD3, 0x00,            /* 3. Set display offset = 0 */
        0x40,                  /* 4. Set start line = 0 */
        0x20, 0x00,            /* 5. Addressing mode = Horizontal */
        0xA1,                  /* 6. Segment remap (дзеркало по X) */
        0xC8,                  /* 7. COM scan direction (дзеркало по Y) */
        0xDA, comPins,         /* 8. COM pins config */
        0x81, 0x7F,            /* 9. Contrast */
        0xD9, 0xF1,            /* 10. Pre-charge period */
        0xDB, 0x40,            /* 11. VCOMH deselect level */
        0xA4,                  /* 12. Resume to RAM content */
        0xA6,                  /* 13. Normal (не інверсний) режим */
        0x8D, 0x14,            /* 14. Charge pump ON */
        0xAF,                  /* 15. Display ON */
    };
    HAL_StatusTypeDef ret = SSD1306_Commands(d, seq, sizeof(seq));
    if (ret != HAL_OK) {
        return ret;
    }
    HAL_Delay(100);  /* даємо charge pump стабілізуватись */
    SSD1306_Clear(d, 0x00);
    ret = SSD1306_Flush(d);
    d->ready = (ret == HAL_OK);  /* дисплей готовий лише за успішної ініціалізації */
    return ret;
}

/* --- Зручні налаштування --- */
static inline HAL_StatusTypeDef SSD1306_SetContrast(Ssd1306 *d, uint8_t value) {
    const uint8_t seq[] = {0x81, value};
    return SSD1306_Commands(d, seq, sizeof(seq));
}
static inline HAL_StatusTypeDef SSD1306_DisplayOn(Ssd1306 *d)  { return SSD1306_Command(d, 0xAF); }
static inline HAL_StatusTypeDef SSD1306_DisplayOff(Ssd1306 *d) { return SSD1306_Command(d, 0xAE); }
static inline HAL_StatusTypeDef SSD1306_Invert(Ssd1306 *d, uint8_t on) {
    return SSD1306_Command(d, on ? 0xA7 : 0xA6);
}

/* Малювання тексту винесено в окремий модуль TextRenderer (text_renderer.h),
 * щоб драйвер лишався мінімальним і не знав про шрифти. */

#endif /* SSD1306_H */
