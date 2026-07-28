#ifndef EEPROM_H
#define EEPROM_H

#include "stm32f4xx_hal.h"

/* ============================================================================
 *  AT24Cxx EEPROM driver (I2C) — header-only, STM32 HAL
 * ============================================================================
 *  Пам'ять адресується 16-бітними адресами (I2C_MEMADD_SIZE_16BIT).
 *  Перші два байти (0x0000..0x0001) зарезервовані під вказівник наступного
 *  запису (Big-Endian). Дані журналу починаються з 0x0002.
 *  I2C-периферію (MX_I2C1_Init) треба ініціалізувати ЗОВНІ до використання.
 * ============================================================================ */

#define EEPROM_ADDR      (0x50 << 1)  /* зсунута 7-бітна адреса для HAL */
#define EEPROM_DATA_START 0x0002      /* перша адреса для даних (після вказівника) */
#define EEPROM_DATA_END   0x0FFF      /* остання доступна адреса */

/* Читання 1 байта */
static inline uint8_t EEPROM_ReadByte(I2C_HandleTypeDef *hi2c, uint16_t mem_address) {
    uint8_t data = 0;
    HAL_I2C_Mem_Read(hi2c, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT, &data, 1, 100);
    return data;
}

/* Запис 1 байта. Повертає HAL-статус: виклик мусить перевіряти його перед тим,
 * як просувати вказівник журналу — інакше зірваний обмін (NACK/таймаут) мовчки
 * лишає непрописаний байт, а вказівник вже вказує далі на нього. */
static inline HAL_StatusTypeDef EEPROM_WriteByte(I2C_HandleTypeDef *hi2c, uint16_t mem_address, uint8_t data) {
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(hi2c, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT, &data, 1, 100);
    if (ret == HAL_OK) {
        HAL_Delay(5); /* Затримка 5 мс для фізичного запису в EEPROM (обов'язково!) */
    }
    return ret;
}

/* Читання вказівника (адреси наступного запису) з перших двох байтів пам'яті */
static inline uint16_t EEPROM_GetNextAddress(I2C_HandleTypeDef *hi2c) {
    uint8_t buf[2];
    HAL_I2C_Mem_Read(hi2c, EEPROM_ADDR, 0x0000, I2C_MEMADD_SIZE_16BIT, buf, 2, 100);

    /* Об'єднання за стандартом Big-Endian */
    uint16_t ptr = ((uint16_t)buf[0] << 8) | buf[1];

    /* Якщо пам'ять чиста (0xFFFF) або адреса некоректна, починаємо з початку даних */
    if (ptr < EEPROM_DATA_START || ptr > EEPROM_DATA_END) {
        return EEPROM_DATA_START;
    }
    return ptr;
}

/* Збереження нового вказівника. Повертає HAL-статус (див. EEPROM_WriteByte). */
static inline HAL_StatusTypeDef EEPROM_SaveNextAddress(I2C_HandleTypeDef *hi2c, uint16_t ptr) {
    uint8_t buf[2];
    buf[0] = (ptr >> 8) & 0xFF; /* MSB (старший байт) */
    buf[1] = ptr & 0xFF;        /* LSB (молодший байт) */

    HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(hi2c, EEPROM_ADDR, 0x0000, I2C_MEMADD_SIZE_16BIT, buf, 2, 100);
    if (ret == HAL_OK) {
        HAL_Delay(5);
    }
    return ret;
}

#endif /* EEPROM_H */
