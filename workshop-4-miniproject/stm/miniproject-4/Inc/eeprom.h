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
#define EEPROM_PTR_ADDR   0x0000      /* де лежить вказівник наступного запису */
#define EEPROM_PTR_SIZE   2           /* його розмір у байтах (Big-Endian) */
#define EEPROM_DATA_START 0x0002      /* перша адреса для даних (після вказівника) */
#define EEPROM_DATA_END   0x0FFF      /* остання доступна адреса */

/* Дедлайн одного обміну (мс): 1-2 байти на 100 кГц — з великим запасом. */
#define EEPROM_I2C_TIMEOUT 100

/* Пауза на внутрішній цикл запису мікросхеми (tWR). Поки він іде, EEPROM не
 * відповідає на шині — без цієї паузи наступний обмін отримає NACK. */
#define EEPROM_WRITE_CYCLE_MS 5

/* Розкладка 16-бітного вказівника на два байти (Big-Endian). */
#define EEPROM_PTR_HI_SHIFT 8
#define EEPROM_BYTE_MASK    0xFF

/* Читання 1 байта */
static inline uint8_t EEPROM_ReadByte(I2C_HandleTypeDef *hi2c, uint16_t mem_address) {
    uint8_t data = 0;
    HAL_I2C_Mem_Read(hi2c, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT,
                     &data, sizeof(data), EEPROM_I2C_TIMEOUT);
    return data;
}

/* Запис 1 байта. Повертає HAL-статус: виклик мусить перевіряти його перед тим,
 * як просувати вказівник журналу — інакше зірваний обмін (NACK/таймаут) мовчки
 * лишає непрописаний байт, а вказівник вже вказує далі на нього. */
static inline HAL_StatusTypeDef EEPROM_WriteByte(I2C_HandleTypeDef *hi2c, uint16_t mem_address, uint8_t data) {
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(hi2c, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT,
                                              &data, sizeof(data), EEPROM_I2C_TIMEOUT);
    if (ret == HAL_OK) {
        HAL_Delay(EEPROM_WRITE_CYCLE_MS); /* чекаємо фізичний запис у EEPROM (обов'язково!) */
    }
    return ret;
}

/* Читання вказівника (адреси наступного запису) з перших двох байтів пам'яті */
static inline uint16_t EEPROM_GetNextAddress(I2C_HandleTypeDef *hi2c) {
    uint8_t buf[EEPROM_PTR_SIZE];
    HAL_I2C_Mem_Read(hi2c, EEPROM_ADDR, EEPROM_PTR_ADDR, I2C_MEMADD_SIZE_16BIT,
                     buf, sizeof(buf), EEPROM_I2C_TIMEOUT);

    /* Об'єднання за стандартом Big-Endian */
    uint16_t ptr = (uint16_t)((uint16_t)buf[0] << EEPROM_PTR_HI_SHIFT) | buf[1];

    /* Якщо пам'ять чиста (0xFFFF) або адреса некоректна, починаємо з початку даних */
    if (ptr < EEPROM_DATA_START || ptr > EEPROM_DATA_END) {
        return EEPROM_DATA_START;
    }
    return ptr;
}

/* Збереження нового вказівника. Повертає HAL-статус (див. EEPROM_WriteByte). */
static inline HAL_StatusTypeDef EEPROM_SaveNextAddress(I2C_HandleTypeDef *hi2c, uint16_t ptr) {
    uint8_t buf[EEPROM_PTR_SIZE];
    buf[0] = (ptr >> EEPROM_PTR_HI_SHIFT) & EEPROM_BYTE_MASK; /* MSB (старший байт) */
    buf[1] = ptr & EEPROM_BYTE_MASK;                          /* LSB (молодший байт) */

    HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(hi2c, EEPROM_ADDR, EEPROM_PTR_ADDR, I2C_MEMADD_SIZE_16BIT,
                                              buf, sizeof(buf), EEPROM_I2C_TIMEOUT);
    if (ret == HAL_OK) {
        HAL_Delay(EEPROM_WRITE_CYCLE_MS);
    }
    return ret;
}

#endif /* EEPROM_H */
