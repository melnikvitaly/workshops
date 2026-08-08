#ifndef EEPROM_H
#define EEPROM_H

#include "stm32f4xx_hal.h"

/* ============================================================================
 *  AT24Cxx EEPROM driver (I2C) — header-only, STM32 HAL
 * ============================================================================
 *  Пам'ять адресується 16-бітними адресами (I2C_MEMADD_SIZE_16BIT) — так
 *  працюють AT24C32/64 (4/8 КБ) на модулі DS1307/DS3231.
 *
 *  Розкладка пам'яті в цьому воркшопі:
 *      0x0000..0x0001  вказівник наступного вільного запису (Big-Endian)
 *      0x0002..0x0007  зарезервовано (вирівнювання)
 *      0x0008..0x0FFF  журнал: записи по EEPROM_RECORD_SIZE байтів
 *
 *  Дані починаються з адреси, кратної розміру запису, — тоді жоден запис не
 *  перетинає межу сторінки (32 байти в AT24C32) і його можна залити одним
 *  обміном. Інакше довелося б розбивати запис на два, бо всередині мікросхеми
 *  адреса при записі загортається в межах сторінки, а не переходить у наступну.
 *
 *  I2C-периферію (MX_I2C1_Init) треба ініціалізувати ЗОВНІ до використання.
 * ============================================================================ */

#define EEPROM_ADDR_7BIT  0x50        /* адреса на шині (така, як показує сканер) */
#define EEPROM_ADDR_SHIFT 1           /* HAL чекає адресу, зсунуту під біт R/W */
#define EEPROM_ADDR      (EEPROM_ADDR_7BIT << EEPROM_ADDR_SHIFT)
#define EEPROM_PTR_ADDR   0x0000      /* де лежить вказівник наступного запису */
#define EEPROM_PTR_SIZE   2           /* його розмір у байтах (Big-Endian) */
#define EEPROM_RECORD_SIZE 8          /* один рядок журналу: час + освітлення */
#define EEPROM_DATA_START 0x0008      /* перша адреса даних (кратна розміру запису) */
#define EEPROM_DATA_END   0x0FFF      /* остання доступна адреса (AT24C32, 4 КБ) */

/* Перша адреса ЗА журналом — щоб не писати всюди "END + 1". */
#define EEPROM_DATA_LIMIT (EEPROM_DATA_END + 1)

/* Скільки записів усього влазить у журнал. */
#define EEPROM_MAX_RECORDS ((EEPROM_DATA_LIMIT - EEPROM_DATA_START) / EEPROM_RECORD_SIZE)

/* Значення стертої комірки: нова мікросхема читається як суцільні 0xFF. */
#define EEPROM_ERASED_BYTE 0xFF

/* Дедлайн одного обміну (мс): кілька байтів на 100 кГц — з великим запасом. */
#define EEPROM_I2C_TIMEOUT 100

/* Скільки разів HAL_I2C_IsDeviceReady перепитує мікросхему, перш ніж вважати її
 * відсутньою. Двох спроб досить навіть якщо вона саме доживає цикл запису. */
#define EEPROM_READY_TRIALS 2

/* Пауза на внутрішній цикл запису мікросхеми (tWR). Поки він іде, EEPROM не
 * відповідає на шині — без цієї паузи наступний обмін отримає NACK. */
#define EEPROM_WRITE_CYCLE_MS 5

/* Розкладка 16-бітного вказівника на два байти (Big-Endian). */
#define EEPROM_PTR_HI_SHIFT 8
#define EEPROM_BYTE_MASK    0xFF

/* Читання блока байтів. Читати можна скільки завгодно й через будь-які межі
 * сторінок — обмеження на сторінку стосується лише запису. */
static inline HAL_StatusTypeDef EEPROM_Read(I2C_HandleTypeDef *hi2c, uint16_t mem_address,
                                            uint8_t *data, uint16_t len) {
    return HAL_I2C_Mem_Read(hi2c, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT,
                            data, len, EEPROM_I2C_TIMEOUT);
}

/* Запис блока байтів у межах однієї сторінки (див. коментар про розкладку).
 * Повертає HAL-статус: виклик мусить перевіряти його перед тим, як просувати
 * вказівник журналу — інакше зірваний обмін (NACK/таймаут) мовчки лишає
 * непрописані байти, а вказівник вже вказує за них. */
static inline HAL_StatusTypeDef EEPROM_Write(I2C_HandleTypeDef *hi2c, uint16_t mem_address,
                                             const uint8_t *data, uint16_t len) {
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(hi2c, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT,
                                              (uint8_t *)data, len, EEPROM_I2C_TIMEOUT);
    if (ret == HAL_OK) {
        HAL_Delay(EEPROM_WRITE_CYCLE_MS); /* чекаємо фізичний запис у EEPROM (обов'язково!) */
    }
    return ret;
}

/* Читання вказівника (адреси наступного запису) з перших двох байтів пам'яті */
static inline uint16_t EEPROM_GetNextAddress(I2C_HandleTypeDef *hi2c) {
    uint8_t buf[EEPROM_PTR_SIZE] = {EEPROM_ERASED_BYTE, EEPROM_ERASED_BYTE};
    EEPROM_Read(hi2c, EEPROM_PTR_ADDR, buf, sizeof(buf));

    /* Об'єднання за стандартом Big-Endian */
    uint16_t ptr = (uint16_t)((uint16_t)buf[0] << EEPROM_PTR_HI_SHIFT) | buf[1];

    /* Якщо пам'ять чиста (0xFFFF), адреса поза журналом або не кратна розміру
     * запису (сміття від попередньої прошивки) — починаємо з початку даних. */
    if (ptr < EEPROM_DATA_START || ptr > EEPROM_DATA_END ||
        ((ptr - EEPROM_DATA_START) % EEPROM_RECORD_SIZE) != 0) {
        return EEPROM_DATA_START;
    }
    return ptr;
}

/* Збереження нового вказівника. Повертає HAL-статус (див. EEPROM_Write). */
static inline HAL_StatusTypeDef EEPROM_SaveNextAddress(I2C_HandleTypeDef *hi2c, uint16_t ptr) {
    uint8_t buf[EEPROM_PTR_SIZE];
    buf[0] = (ptr >> EEPROM_PTR_HI_SHIFT) & EEPROM_BYTE_MASK; /* MSB (старший байт) */
    buf[1] = ptr & EEPROM_BYTE_MASK;                          /* LSB (молодший байт) */

    return EEPROM_Write(hi2c, EEPROM_PTR_ADDR, buf, sizeof(buf));
}

/* Чи є мікросхема на шині (зручно показати на старті, що все підключено). */
static inline HAL_StatusTypeDef EEPROM_IsReady(I2C_HandleTypeDef *hi2c) {
    return HAL_I2C_IsDeviceReady(hi2c, EEPROM_ADDR, EEPROM_READY_TRIALS, EEPROM_I2C_TIMEOUT);
}

#endif /* EEPROM_H */
