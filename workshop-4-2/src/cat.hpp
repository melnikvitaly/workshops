#pragma once

#include <cstdint>
#include <cstdlib>
#include "ssd1306.hpp"

// ============================================================================
//  Cat — малює мордочку кота за допомогою драйвера SSD1306
// ============================================================================
//  Використовує лише публічний API Ssd1306 (drawPixel + flush). Власні
//  примітиви (лінія/коло) реалізовані тут, щоб драйвер лишався мінімальним.
//
//  Приклад:
//      Ssd1306 oled(I2C_NUM_0);
//      oled.init();
//      Cat cat(oled);
//      cat.draw();   // по центру екрана
// ============================================================================

class Cat {
public:
    explicit Cat(Ssd1306 &oled) : oled_(oled) {}

    // Намалювати кота з центром у (cx, cy) і "радіусом" голови r.
    // earTilt зсуває кінчики вух по горизонталі (-=всередину, +=назовні) —
    // використовується для анімації "поворухнути вухами".
    // За замовчуванням — по центру екрана. Викликає flush() самостійно.
    // flush=false малює кота в буфер, але не відправляє на екран — щоб поверх
    // нього можна було домалювати інше (напр. годинник) і викликати flush() раз.
    void draw(int16_t cx = -1, int16_t cy = -1, int16_t r = 20,
              int16_t earTilt = 0, bool flush = true) {
        if (cx < 0) cx = oled_.width() / 2;
        if (cy < 0) cy = oled_.height() / 2;

        oled_.clear();

        // --- Голова (коло) ---
        circle(cx, cy, r);

        // --- Вуха (трикутники зверху, спираються на коло) ---
        const int16_t earH = r;        // висота вуха
        const int16_t earW = r * 2 / 3;  // піврозмах основи
        // ліве вухо (кінчик ворушиться на earTilt)
        triangle(cx - r + 2,        cy - r + 4,
                 cx - r + 2 + earW, cy - r + 4,
                 cx - r + 2 - earTilt, cy - r + 4 - earH);
        // праве вухо (дзеркально)
        triangle(cx + r - 2,        cy - r + 4,
                 cx + r - 2 - earW, cy - r + 4,
                 cx + r - 2 + earTilt, cy - r + 4 - earH);

        // --- Очі (заповнені кружечки) ---
        const int16_t eyeDx = r / 2;
        const int16_t eyeDy = r / 5;
        fillCircle(cx - eyeDx, cy - eyeDy, 3);
        fillCircle(cx + eyeDx, cy - eyeDy, 3);

        // --- Ніс (маленький трикутник вершиною вниз) ---
        const int16_t noseY = cy + r / 5;
        triangle(cx - 3, noseY, cx + 3, noseY, cx, noseY + 4);

        // --- Рот (дві дуги-лінії від носа) ---
        line(cx, noseY + 4, cx - 5, noseY + 7);
        line(cx, noseY + 4, cx + 5, noseY + 7);

        // --- Вуса (по три з кожного боку) ---
        const int16_t wy = noseY + 1;
        for (int i = 0; i < 3; ++i) {
            const int16_t dy = (i - 1) * 4;
            line(cx - 6,  wy + dy, cx - r - 6, wy + dy * 2);  // ліві
            line(cx + 6,  wy + dy, cx + r + 6, wy + dy * 2);  // праві
        }

        if (flush) oled_.flush();
    }

    // Анімація: кіт ворушить вухами (кінчики гойдаються вперед-назад).
    // cycles — скільки повних "хитань", stepMs — затримка між кадрами.
    void animateEars(int cycles = 3, int stepMs = 60,
                     int16_t cx = -1, int16_t cy = -1, int16_t r = 20) {
        // Фази нахилу: 0 -> назовні -> 0 -> всередину -> 0
        static const int16_t kPhases[] = {0, 3, 6, 3, 0, -3, -6, -3};
        const int kCount = sizeof(kPhases) / sizeof(kPhases[0]);
        for (int c = 0; c < cycles; ++c) {
            for (int i = 0; i < kCount; ++i) {
                draw(cx, cy, r, kPhases[i]);
                vTaskDelay(stepMs / portTICK_PERIOD_MS);
            }
        }
    }

private:
    // --- Примітиви на основі Ssd1306::drawPixel ---

    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
        // Алгоритм Брезенхема
        int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int16_t err = dx + dy;
        while (true) {
            oled_.drawPixel(x0, y0, true);
            if (x0 == x1 && y0 == y1) break;
            int16_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2) {
        line(x0, y0, x1, y1);
        line(x1, y1, x2, y2);
        line(x2, y2, x0, y0);
    }

    void circle(int16_t cx, int16_t cy, int16_t r) {
        // Алгоритм кола Брезенхема (контур)
        int16_t x = r, y = 0, err = 1 - r;
        while (x >= y) {
            plot8(cx, cy, x, y);
            ++y;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                --x;
                err += 2 * (y - x) + 1;
            }
        }
    }

    void fillCircle(int16_t cx, int16_t cy, int16_t r) {
        for (int16_t y = -r; y <= r; ++y) {
            for (int16_t x = -r; x <= r; ++x) {
                if (x * x + y * y <= r * r) {
                    oled_.drawPixel(cx + x, cy + y, true);
                }
            }
        }
    }

    void plot8(int16_t cx, int16_t cy, int16_t x, int16_t y) {
        oled_.drawPixel(cx + x, cy + y, true);
        oled_.drawPixel(cx - x, cy + y, true);
        oled_.drawPixel(cx + x, cy - y, true);
        oled_.drawPixel(cx - x, cy - y, true);
        oled_.drawPixel(cx + y, cy + x, true);
        oled_.drawPixel(cx - y, cy + x, true);
        oled_.drawPixel(cx + y, cy - x, true);
        oled_.drawPixel(cx - y, cy - x, true);
    }

    Ssd1306 &oled_;
};
