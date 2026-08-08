#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include <stdio.h>
#include "stm32f4xx_hal.h"

#define I2C_SCANNER_MAX_DEVICES 8  /* ємність внутрішнього буфера адрес */

/* Діапазон 7-бітних адрес, які має сенс пінгувати: 0x00 — загальний виклик
 * (general call), 0x78..0x7F зарезервовані стандартом I2C. */
#define I2C_SCANNER_FIRST_ADDR 1
#define I2C_SCANNER_LAST_ADDR  126

/* Пінг — це START + адреса + STOP. Одна спроба і короткий дедлайн: пристрій або
 * відповідає ACK одразу, або його немає. Довший таймаут лише розтягнув би скан
 * (126 адрес) і заблокував головний цикл. */
#define I2C_SCANNER_PING_TRIALS  1
#define I2C_SCANNER_PING_TIMEOUT 5

#define I2C_SCANNER_NONE_FOUND "0x00"  /* що показати, якщо шина порожня */

/* ============================================================================
 *  I2C Scanner — header-only, STM32 HAL
 * ============================================================================
 *  Перебирає адреси 1..126 і пінгує кожну через HAL_I2C_IsDeviceReady
 *  (START + адреса + STOP). Якщо пристрій відповів ACK — він присутній.
 *  I2C-периферію треба ініціалізувати ЗОВНІ до використання.
 * ============================================================================ */

/* Перевірити, чи відповідає пристрій за конкретною 7-бітною адресою */
static inline int I2CScanner_Ping(I2C_HandleTypeDef *hi2c, uint8_t addr7) {
    return HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(addr7 << 1),
                                 I2C_SCANNER_PING_TRIALS, I2C_SCANNER_PING_TIMEOUT) == HAL_OK;
}

/* Просканувати всю шину. Повертає кількість знайдених пристроїв.
 * Якщо передати out/maxOut — знайдені 7-бітні адреси записуються у масив out
 * (не більше maxOut штук), щоб їх можна було показати на екрані. */
static inline int I2CScanner_Scan(I2C_HandleTypeDef *hi2c, uint8_t *out, int maxOut) {
    int found = 0;
    for (uint8_t addr = I2C_SCANNER_FIRST_ADDR; addr <= I2C_SCANNER_LAST_ADDR; ++addr) {
        if (I2CScanner_Ping(hi2c, addr)) {
            if (out && found < maxOut) {
                out[found] = addr;
            }
            ++found;
        }
    }
    return found;
}

/* Просканувати шину й одразу сформувати рядок адрес "0x3C 0x68 ..." для показу
 * на екрані (не більше maxShown адрес). Якщо нічого не знайдено — записуємо
 * "0x00". Повертає загальну кількість знайдених пристроїв. */
static inline int I2CScanner_ScanToString(I2C_HandleTypeDef *hi2c, char *out, size_t outLen, int maxShown) {
    if (maxShown > I2C_SCANNER_MAX_DEVICES) {
        maxShown = I2C_SCANNER_MAX_DEVICES;
    }
    uint8_t addrs[I2C_SCANNER_MAX_DEVICES];
    const int found = I2CScanner_Scan(hi2c, addrs, maxShown);

    const int shown = (found < maxShown) ? found : maxShown;
    int pos = 0;
    out[0] = '\0';
    /* pos must never exceed outLen: outLen is size_t, so a later "outLen - pos"
     * with pos > outLen would underflow to a huge unsigned size and hand
     * snprintf a bogus length, overrunning the buffer. snprintf's return value
     * is how many bytes it WOULD have written, which can exceed the space
     * given on truncation, so clamp pos back down every iteration. */
    for (int i = 0; i < shown && (size_t)pos < outLen; ++i) {
        int n = snprintf(out + pos, outLen - pos, "%s0x%02X", (i ? " " : ""), addrs[i]);
        if (n < 0) {
            break;
        }
        pos += n;
        if ((size_t)pos > outLen) {
            pos = (int)outLen;
        }
    }
    if (found == 0) {
        snprintf(out, outLen, I2C_SCANNER_NONE_FOUND);  /* нічого не знайдено */
    }
    return found;
}

#endif /* I2C_SCANNER_H */
