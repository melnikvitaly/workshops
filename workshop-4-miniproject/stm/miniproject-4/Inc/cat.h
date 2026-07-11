#ifndef CAT_H
#define CAT_H

#include <stdlib.h>
#include "ssd1306.h"

/* ============================================================================
 *  Cat — малює мордочку кота за допомогою драйвера SSD1306
 * ============================================================================
 *  Використовує лише публічний API Ssd1306 (SSD1306_DrawPixel + SSD1306_Flush).
 *  Власні примітиви (лінія/коло) реалізовані тут, щоб драйвер лишався мінімальним.
 * ============================================================================ */

/* --- Примітиви на основі SSD1306_DrawPixel --- */

static inline void Cat_Line(Ssd1306 *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    /* Алгоритм Брезенхема */
    int16_t dx = (int16_t)abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = (int16_t)-abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;
    while (1) {
        SSD1306_DrawPixel(d, x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = (int16_t)(2 * err);
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static inline void Cat_Triangle(Ssd1306 *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                int16_t x2, int16_t y2) {
    Cat_Line(d, x0, y0, x1, y1);
    Cat_Line(d, x1, y1, x2, y2);
    Cat_Line(d, x2, y2, x0, y0);
}

static inline void Cat_Plot8(Ssd1306 *d, int16_t cx, int16_t cy, int16_t x, int16_t y) {
    SSD1306_DrawPixel(d, cx + x, cy + y, 1);
    SSD1306_DrawPixel(d, cx - x, cy + y, 1);
    SSD1306_DrawPixel(d, cx + x, cy - y, 1);
    SSD1306_DrawPixel(d, cx - x, cy - y, 1);
    SSD1306_DrawPixel(d, cx + y, cy + x, 1);
    SSD1306_DrawPixel(d, cx - y, cy + x, 1);
    SSD1306_DrawPixel(d, cx + y, cy - x, 1);
    SSD1306_DrawPixel(d, cx - y, cy - x, 1);
}

static inline void Cat_Circle(Ssd1306 *d, int16_t cx, int16_t cy, int16_t r) {
    /* Алгоритм кола Брезенхема (контур) */
    int16_t x = r, y = 0, err = (int16_t)(1 - r);
    while (x >= y) {
        Cat_Plot8(d, cx, cy, x, y);
        ++y;
        if (err < 0) {
            err += (int16_t)(2 * y + 1);
        } else {
            --x;
            err += (int16_t)(2 * (y - x) + 1);
        }
    }
}

static inline void Cat_FillCircle(Ssd1306 *d, int16_t cx, int16_t cy, int16_t r) {
    for (int16_t y = -r; y <= r; ++y) {
        for (int16_t x = -r; x <= r; ++x) {
            if (x * x + y * y <= r * r) {
                SSD1306_DrawPixel(d, cx + x, cy + y, 1);
            }
        }
    }
}

/* Намалювати кота з центром у (cx, cy) і "радіусом" голови r.
 * earTilt зсуває кінчики вух по горизонталі (анімація "поворухнути вухами").
 * flush=0 малює кота в буфер, але не відправляє на екран — щоб поверх нього
 * можна було домалювати інше (напр. годинник) і викликати SSD1306_Flush() раз. */
static inline void Cat_Draw(Ssd1306 *d, int16_t cx, int16_t cy, int16_t r,
                            int16_t earTilt, uint8_t flush) {
    if (cx < 0) cx = d->width / 2;
    if (cy < 0) cy = d->height / 2;

    SSD1306_Clear(d, 0x00);

    /* --- Голова (коло) --- */
    Cat_Circle(d, cx, cy, r);

    /* --- Вуха (трикутники зверху, спираються на коло) --- */
    const int16_t earH = r;              /* висота вуха */
    const int16_t earW = (int16_t)(r * 2 / 3);  /* піврозмах основи */
    /* ліве вухо (кінчик ворушиться на earTilt) */
    Cat_Triangle(d, cx - r + 2,        cy - r + 4,
                    cx - r + 2 + earW, cy - r + 4,
                    cx - r + 2 - earTilt, cy - r + 4 - earH);
    /* праве вухо (дзеркально) */
    Cat_Triangle(d, cx + r - 2,        cy - r + 4,
                    cx + r - 2 - earW, cy - r + 4,
                    cx + r - 2 + earTilt, cy - r + 4 - earH);

    /* --- Очі (заповнені кружечки) --- */
    const int16_t eyeDx = (int16_t)(r / 2);
    const int16_t eyeDy = (int16_t)(r / 5);
    Cat_FillCircle(d, cx - eyeDx, cy - eyeDy, 3);
    Cat_FillCircle(d, cx + eyeDx, cy - eyeDy, 3);

    /* --- Ніс (маленький трикутник вершиною вниз) --- */
    const int16_t noseY = (int16_t)(cy + r / 5);
    Cat_Triangle(d, cx - 3, noseY, cx + 3, noseY, cx, noseY + 4);

    /* --- Рот (дві дуги-лінії від носа) --- */
    Cat_Line(d, cx, noseY + 4, cx - 5, noseY + 7);
    Cat_Line(d, cx, noseY + 4, cx + 5, noseY + 7);

    /* --- Вуса (по три з кожного боку) --- */
    const int16_t wy = (int16_t)(noseY + 1);
    for (int i = 0; i < 3; ++i) {
        const int16_t dy = (int16_t)((i - 1) * 4);
        Cat_Line(d, cx - 6,  wy + dy, cx - r - 6, wy + dy * 2);  /* ліві */
        Cat_Line(d, cx + 6,  wy + dy, cx + r + 6, wy + dy * 2);  /* праві */
    }

    if (flush) SSD1306_Flush(d);
}

#endif /* CAT_H */
