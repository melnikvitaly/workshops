#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include <stdio.h>
#include "stm32f4xx_hal.h"

#define I2C_SCANNER_MAX_DEVICES 8  /* ємність внутрішнього буфера адрес */

/* ============================================================================
 *  I2C Scanner — header-only, STM32 HAL
 * ============================================================================
 *  Перебирає адреси 1..126 і пінгує кожну через HAL_I2C_IsDeviceReady
 *  (START + адреса + STOP). Якщо пристрій відповів ACK — він присутній.
 *  I2C-периферію треба ініціалізувати ЗОВНІ до використання.
 * ============================================================================ */

/* Перевірити, чи відповідає пристрій за конкретною 7-бітною адресою */
static inline int I2CScanner_Ping(I2C_HandleTypeDef *hi2c, uint8_t addr7) {
    return HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(addr7 << 1), 1, 5) == HAL_OK;
}

/* Просканувати всю шину. Повертає кількість знайдених пристроїв.
 * Якщо передати out/maxOut — знайдені 7-бітні адреси записуються у масив out
 * (не більше maxOut штук), щоб їх можна було показати на екрані. */
static inline int I2CScanner_Scan(I2C_HandleTypeDef *hi2c, uint8_t *out, int maxOut) {
    int found = 0;
    for (uint8_t addr = 1; addr < 127; ++addr) {
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
    for (int i = 0; i < shown; ++i) {
        pos += snprintf(out + pos, outLen - pos, "%s0x%02X", (i ? " " : ""), addrs[i]);
    }
    if (found == 0) {
        snprintf(out, outLen, "0x00");  /* нічого не знайдено */
    }
    return found;
}

#endif /* I2C_SCANNER_H */
