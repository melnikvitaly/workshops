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

/* --- Геометрія GDDRAM --- */
#define SSD1306_PAGE_HEIGHT 8     /* рядків пікселів в одному байті (сторінка) */
#define SSD1306_MAX_WIDTH   128   /* найбільша підтримана ширина панелі */
#define SSD1306_MAX_HEIGHT  64    /* найбільша підтримана висота панелі */
#define SSD1306_HEIGHT_32   32    /* висота, для якої потрібні інші multiplex/COM pins */
#define SSD1306_MAX_BUFFER  (SSD1306_MAX_WIDTH * SSD1306_MAX_HEIGHT / SSD1306_PAGE_HEIGHT)
#define SSD1306_FIRST_COLUMN 0    /* ліва колонка вікна запису */
#define SSD1306_FIRST_PAGE   0    /* верхня сторінка вікна запису */

/* --- Візерунки заповнення кадрового буфера (SSD1306_Clear) --- */
#define SSD1306_FILL_BLANK 0x00   /* усі пікселі згашені */
#define SSD1306_FILL_SOLID 0xFF   /* усі пікселі засвічені */

/* --- Коди команд (нумерація — кроки з плану ініціалізації вище) --- */
#define SSD1306_CMD_DISPLAY_OFF        0xAE  /* 1  */
#define SSD1306_CMD_SET_MULTIPLEX      0xA8  /* 2  */
#define SSD1306_CMD_SET_DISPLAY_OFFSET 0xD3  /* 3  */
#define SSD1306_CMD_SET_START_LINE     0x40  /* 4  (start line = 0) */
#define SSD1306_CMD_SET_MEM_MODE       0x20  /* 5  */
#define SSD1306_CMD_SEG_REMAP          0xA1  /* 6  дзеркало по X */
#define SSD1306_CMD_COM_SCAN_DEC       0xC8  /* 7  дзеркало по Y */
#define SSD1306_CMD_SET_COM_PINS       0xDA  /* 8  */
#define SSD1306_CMD_SET_CONTRAST       0x81  /* 9  */
#define SSD1306_CMD_SET_PRECHARGE      0xD9  /* 10 */
#define SSD1306_CMD_SET_VCOM_DETECT    0xDB  /* 11 */
#define SSD1306_CMD_RESUME_RAM         0xA4  /* 12 */
#define SSD1306_CMD_NORMAL_DISPLAY     0xA6  /* 13 */
#define SSD1306_CMD_INVERT_DISPLAY     0xA7  /* 13' інверсія */
#define SSD1306_CMD_CHARGE_PUMP        0x8D  /* 14 */
#define SSD1306_CMD_DISPLAY_ON         0xAF  /* 15 */
#define SSD1306_CMD_SET_COL_ADDR       0x21  /* вікно запису: колонки */
#define SSD1306_CMD_SET_PAGE_ADDR      0x22  /* вікно запису: сторінки */

/* --- Аргументи команд --- */
#define SSD1306_MEM_MODE_HORIZONTAL   0x00  /* до п.5: горизонтальна адресація */
#define SSD1306_DISPLAY_OFFSET_NONE   0x00  /* до п.3 */
#define SSD1306_MULTIPLEX_32ROW       0x1F  /* до п.2 для панелі 128x32 */
#define SSD1306_MULTIPLEX_64ROW       0x3F  /* до п.2 для панелі 128x64 */
#define SSD1306_COM_PINS_32ROW        0x02  /* до п.8 для панелі 128x32 */
#define SSD1306_COM_PINS_64ROW        0x12  /* до п.8 для панелі 128x64 */
#define SSD1306_CONTRAST_DEFAULT      0x7F  /* до п.9: середина діапазону */
#define SSD1306_PRECHARGE_DEFAULT     0xF1  /* до п.10 */
#define SSD1306_VCOM_DESELECT_DEFAULT 0x40  /* до п.11 */
#define SSD1306_CHARGE_PUMP_ON        0x14  /* до п.14 */

/* Скільки чекати після ввімкнення charge pump, доки підніметься напруга панелі. */
#define SSD1306_CHARGE_PUMP_DELAY_MS 100

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
    /* Сторінкова упаковка: байт містить SSD1306_PAGE_HEIGHT пікселів по вертикалі. */
    uint16_t index = (uint16_t)x + (uint16_t)(y / SSD1306_PAGE_HEIGHT) * d->width;
    uint8_t  bit   = (uint8_t)(1 << (y & (SSD1306_PAGE_HEIGHT - 1)));
    if (on) {
        d->buffer[index] |= bit;
    } else {
        d->buffer[index] &= (uint8_t)~bit;
    }
}

/* Виставляємо вікно запису на весь екран і відправляємо буфер. */
static inline HAL_StatusTypeDef SSD1306_Flush(Ssd1306 *d) {
    const uint8_t window[] = {
        SSD1306_CMD_SET_COL_ADDR,  SSD1306_FIRST_COLUMN,
        (uint8_t)(d->width - 1),                                  /* column 0..W-1 */
        SSD1306_CMD_SET_PAGE_ADDR, SSD1306_FIRST_PAGE,
        (uint8_t)(d->height / SSD1306_PAGE_HEIGHT - 1),           /* page 0..pages-1 */
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
    d->bufferLen = (uint16_t)((uint16_t)width * height / SSD1306_PAGE_HEIGHT);
    if (d->bufferLen > SSD1306_MAX_BUFFER) {
        d->bufferLen = SSD1306_MAX_BUFFER;
    }
    SSD1306_Clear(d, SSD1306_FILL_BLANK);
}

/* Повний план ініціалізації. I2C має бути ініціалізовано ЗОВНІ до виклику. */
static inline HAL_StatusTypeDef SSD1306_Init(Ssd1306 *d) {
    const uint8_t multiplex = (d->height == SSD1306_HEIGHT_32) ? SSD1306_MULTIPLEX_32ROW
                                                               : SSD1306_MULTIPLEX_64ROW;
    const uint8_t comPins   = (d->height == SSD1306_HEIGHT_32) ? SSD1306_COM_PINS_32ROW
                                                               : SSD1306_COM_PINS_64ROW;
    const uint8_t seq[] = {
        SSD1306_CMD_DISPLAY_OFF,                                    /* 1  */
        SSD1306_CMD_SET_MULTIPLEX,      multiplex,                  /* 2  */
        SSD1306_CMD_SET_DISPLAY_OFFSET, SSD1306_DISPLAY_OFFSET_NONE,/* 3  */
        SSD1306_CMD_SET_START_LINE,                                 /* 4  */
        SSD1306_CMD_SET_MEM_MODE,       SSD1306_MEM_MODE_HORIZONTAL,/* 5  */
        SSD1306_CMD_SEG_REMAP,                                      /* 6  */
        SSD1306_CMD_COM_SCAN_DEC,                                   /* 7  */
        SSD1306_CMD_SET_COM_PINS,       comPins,                    /* 8  */
        SSD1306_CMD_SET_CONTRAST,       SSD1306_CONTRAST_DEFAULT,   /* 9  */
        SSD1306_CMD_SET_PRECHARGE,      SSD1306_PRECHARGE_DEFAULT,  /* 10 */
        SSD1306_CMD_SET_VCOM_DETECT,    SSD1306_VCOM_DESELECT_DEFAULT, /* 11 */
        SSD1306_CMD_RESUME_RAM,                                     /* 12 */
        SSD1306_CMD_NORMAL_DISPLAY,                                 /* 13 */
        SSD1306_CMD_CHARGE_PUMP,        SSD1306_CHARGE_PUMP_ON,     /* 14 */
        SSD1306_CMD_DISPLAY_ON,                                     /* 15 */
    };
    HAL_StatusTypeDef ret = SSD1306_Commands(d, seq, sizeof(seq));
    if (ret != HAL_OK) {
        return ret;
    }
    HAL_Delay(SSD1306_CHARGE_PUMP_DELAY_MS);  /* даємо charge pump стабілізуватись */
    SSD1306_Clear(d, SSD1306_FILL_BLANK);
    ret = SSD1306_Flush(d);
    d->ready = (ret == HAL_OK);  /* дисплей готовий лише за успішної ініціалізації */
    return ret;
}

/* --- Зручні налаштування --- */
static inline HAL_StatusTypeDef SSD1306_SetContrast(Ssd1306 *d, uint8_t value) {
    const uint8_t seq[] = {SSD1306_CMD_SET_CONTRAST, value};
    return SSD1306_Commands(d, seq, sizeof(seq));
}
static inline HAL_StatusTypeDef SSD1306_DisplayOn(Ssd1306 *d)  { return SSD1306_Command(d, SSD1306_CMD_DISPLAY_ON); }
static inline HAL_StatusTypeDef SSD1306_DisplayOff(Ssd1306 *d) { return SSD1306_Command(d, SSD1306_CMD_DISPLAY_OFF); }
static inline HAL_StatusTypeDef SSD1306_Invert(Ssd1306 *d, uint8_t on) {
    return SSD1306_Command(d, on ? SSD1306_CMD_INVERT_DISPLAY : SSD1306_CMD_NORMAL_DISPLAY);
}

/* Малювання тексту винесено в окремий модуль TextRenderer (text_renderer.h),
 * щоб драйвер лишався мінімальним і не знав про шрифти. */

#endif /* SSD1306_H */
