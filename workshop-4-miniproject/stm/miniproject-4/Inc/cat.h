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

/* --- Пропорції мордочки (усе, крім вух, прив'язано до "радіуса" голови r) --- */
#define CAT_EAR_INSET      2   /* наскільки основа вуха відступає від краю кола */
#define CAT_EAR_DROP       4   /* наскільки основа вуха опущена нижче верху кола */
#define CAT_EAR_BASE_NUM   2   /* піврозмах основи вуха = r * NUM / DEN */
#define CAT_EAR_BASE_DEN   3
#define CAT_EYE_DX_DEN     2   /* очі рознесені на r / DEN від центра */
#define CAT_EYE_DY_DEN     5   /* і підняті на r / DEN над центром */
#define CAT_EYE_RADIUS     3
#define CAT_NOSE_DY_DEN    5   /* ніс опущений на r / DEN під центр */
#define CAT_NOSE_HALF_W    3   /* півширина основи носа */
#define CAT_NOSE_HEIGHT    4   /* від основи до кінчика (вершина дивиться вниз) */
#define CAT_MOUTH_DX       5   /* розмах кутиків рота від кінчика носа */
#define CAT_MOUTH_DY       3   /* і наскільки вони опущені */
#define CAT_WHISKERS       3   /* вусів з кожного боку */
#define CAT_WHISKER_STEP   4   /* вертикальний крок між вусами */
#define CAT_WHISKER_INSET  6   /* відступ внутрішнього кінця вуса від центра */
#define CAT_WHISKER_LEN    6   /* наскільки зовнішній кінець виходить за коло */
#define CAT_WHISKER_TOP_DY 1   /* зсув пучка вусів відносно носа */
#define CAT_WHISKER_SPREAD 2   /* нахил вуса: зовнішній кінець відхиляється вдвічі */

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

    SSD1306_Clear(d, SSD1306_FILL_BLANK);

    /* --- Голова (коло) --- */
    Cat_Circle(d, cx, cy, r);

    /* --- Вуха (трикутники зверху, спираються на коло) --- */
    const int16_t earH  = r;                                              /* висота вуха */
    const int16_t earW  = (int16_t)(r * CAT_EAR_BASE_NUM / CAT_EAR_BASE_DEN); /* піврозмах основи */
    const int16_t earX  = (int16_t)(r - CAT_EAR_INSET);                   /* основа від центра по X */
    const int16_t earY  = (int16_t)(cy - r + CAT_EAR_DROP);               /* лінія основи вух */
    /* ліве вухо (кінчик ворушиться на earTilt) */
    Cat_Triangle(d, cx - earX,           earY,
                    cx - earX + earW,    earY,
                    cx - earX - earTilt, earY - earH);
    /* праве вухо (дзеркально) */
    Cat_Triangle(d, cx + earX,           earY,
                    cx + earX - earW,    earY,
                    cx + earX + earTilt, earY - earH);

    /* --- Очі (заповнені кружечки) --- */
    const int16_t eyeDx = (int16_t)(r / CAT_EYE_DX_DEN);
    const int16_t eyeDy = (int16_t)(r / CAT_EYE_DY_DEN);
    Cat_FillCircle(d, cx - eyeDx, cy - eyeDy, CAT_EYE_RADIUS);
    Cat_FillCircle(d, cx + eyeDx, cy - eyeDy, CAT_EYE_RADIUS);

    /* --- Ніс (маленький трикутник вершиною вниз) --- */
    const int16_t noseY = (int16_t)(cy + r / CAT_NOSE_DY_DEN);
    const int16_t noseTipY = (int16_t)(noseY + CAT_NOSE_HEIGHT);
    Cat_Triangle(d, cx - CAT_NOSE_HALF_W, noseY, cx + CAT_NOSE_HALF_W, noseY, cx, noseTipY);

    /* --- Рот (дві дуги-лінії від кінчика носа) --- */
    Cat_Line(d, cx, noseTipY, cx - CAT_MOUTH_DX, noseTipY + CAT_MOUTH_DY);
    Cat_Line(d, cx, noseTipY, cx + CAT_MOUTH_DX, noseTipY + CAT_MOUTH_DY);

    /* --- Вуса (по CAT_WHISKERS з кожного боку) --- */
    const int16_t wy = (int16_t)(noseY + CAT_WHISKER_TOP_DY);
    for (int i = 0; i < CAT_WHISKERS; ++i) {
        /* середній вус горизонтальний, крайні розходяться вгору і вниз */
        const int16_t dy = (int16_t)((i - CAT_WHISKERS / 2) * CAT_WHISKER_STEP);
        const int16_t outer = (int16_t)(r + CAT_WHISKER_LEN);
        Cat_Line(d, cx - CAT_WHISKER_INSET, wy + dy,
                    cx - outer,             wy + dy * CAT_WHISKER_SPREAD);  /* ліві */
        Cat_Line(d, cx + CAT_WHISKER_INSET, wy + dy,
                    cx + outer,             wy + dy * CAT_WHISKER_SPREAD);  /* праві */
    }

    if (flush) SSD1306_Flush(d);
}

#endif /* CAT_H */
