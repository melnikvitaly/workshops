#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include "stm32f4xx_hal.h"

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

#endif /* I2C_SCANNER_H */
