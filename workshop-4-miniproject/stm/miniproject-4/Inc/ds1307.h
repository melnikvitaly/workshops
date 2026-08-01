#ifndef DS1307_H
#define DS1307_H

#include <stdio.h>
#include "stm32f4xx_hal.h"

/* ============================================================================
 *  DS1307 RTC driver (I2C) — header-only, STM32 HAL
 * ============================================================================
 *  Карта регістрів (усі значення у BCD):
 *    0x00  секунди (старший біт = CH, Clock Halt)
 *    0x01  хвилини
 *    0x02  години
 *    0x03  день тижня
 *    0x04  число
 *    0x05  місяць
 *    0x06  рік
 *  I2C-периферію (MX_I2C1_Init) треба ініціалізувати ЗОВНІ до використання.
 * ============================================================================ */

#define DS1307_ADDR (0x68 << 1)   /* зсунута 7-бітна адреса для HAL */

/* Карта регістрів: читаємо/пишемо блок від секунд до року одним обміном. */
#define DS1307_REG_SECONDS 0x00
#define DS1307_REG_MINUTES 0x01
#define DS1307_REG_HOURS   0x02
#define DS1307_REG_DAY     0x03
#define DS1307_REG_DATE    0x04
#define DS1307_REG_MONTH   0x05
#define DS1307_REG_YEAR    0x06
#define DS1307_TIME_REGS   7      /* 0x00..0x06 — увесь блок часу */

/* Маски значущих бітів у регістрах (решта — службові біти мікросхеми):
 * у секундах старший біт — CH (Clock Halt), у годинах біт 6 — режим 12/24. */
#define DS1307_MASK_SECONDS 0x7F
#define DS1307_MASK_MINUTES 0x7F
#define DS1307_MASK_HOURS   0x3F  /* 24-годинний формат */
#define DS1307_MASK_DAY     0x07
#define DS1307_MASK_DATE    0x3F
#define DS1307_MASK_MONTH   0x1F

/* Пакування BCD: одна десяткова цифра на півбайт. */
#define DS1307_BCD_SHIFT 4
#define DS1307_BCD_MASK  0x0F
#define DS1307_BCD_BASE  10

/* Дедлайн одного обміну з RTC (мс): кілька байтів на 100 кГц — з великим запасом. */
#define DS1307_I2C_TIMEOUT 100

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;   /* 24-годинний формат */
    uint8_t day;     /* день тижня 1..7 */
    uint8_t date;    /* число 1..31 */
    uint8_t month;   /* 1..12 */
    uint8_t year;    /* 0..99 (роки 2000..2099) */
} RtcTime;

static inline uint8_t DS1307_BcdToDec(uint8_t v) {
    return (uint8_t)((v >> DS1307_BCD_SHIFT) * DS1307_BCD_BASE + (v & DS1307_BCD_MASK));
}
static inline uint8_t DS1307_DecToBcd(uint8_t v) {
    return (uint8_t)(((v / DS1307_BCD_BASE) << DS1307_BCD_SHIFT) | (v % DS1307_BCD_BASE));
}

/* Зчитати лише секунди (зручно для тесту/тіку годинника) */
static inline HAL_StatusTypeDef DS1307_ReadSeconds(I2C_HandleTypeDef *hi2c, uint8_t *seconds) {
    uint8_t raw = 0;
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(hi2c, DS1307_ADDR, DS1307_REG_SECONDS,
                                             I2C_MEMADD_SIZE_8BIT, &raw, sizeof(raw),
                                             DS1307_I2C_TIMEOUT);
    if (ret == HAL_OK) {
        *seconds = DS1307_BcdToDec(raw & DS1307_MASK_SECONDS);  /* відкидаємо біт CH */
    }
    return ret;
}

/* Зчитати повний час */
static inline HAL_StatusTypeDef DS1307_ReadTime(I2C_HandleTypeDef *hi2c, RtcTime *out) {
    uint8_t raw[DS1307_TIME_REGS] = {0};
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(hi2c, DS1307_ADDR, DS1307_REG_SECONDS,
                                             I2C_MEMADD_SIZE_8BIT, raw, sizeof(raw),
                                             DS1307_I2C_TIMEOUT);
    if (ret != HAL_OK) {
        return ret;
    }
    out->seconds = DS1307_BcdToDec(raw[DS1307_REG_SECONDS] & DS1307_MASK_SECONDS);
    out->minutes = DS1307_BcdToDec(raw[DS1307_REG_MINUTES] & DS1307_MASK_MINUTES);
    out->hours   = DS1307_BcdToDec(raw[DS1307_REG_HOURS]   & DS1307_MASK_HOURS);  /* 24-годинний формат */
    out->day     = DS1307_BcdToDec(raw[DS1307_REG_DAY]     & DS1307_MASK_DAY);
    out->date    = DS1307_BcdToDec(raw[DS1307_REG_DATE]    & DS1307_MASK_DATE);
    out->month   = DS1307_BcdToDec(raw[DS1307_REG_MONTH]   & DS1307_MASK_MONTH);
    out->year    = DS1307_BcdToDec(raw[DS1307_REG_YEAR]);
    return HAL_OK;
}

/* Зчитати час і одразу відформатувати у рядок "HH:MM:SS" для показу.
 * out лишається без змін, якщо читання не вдалося (повертає код помилки). */
static inline HAL_StatusTypeDef DS1307_ReadTimeString(I2C_HandleTypeDef *hi2c, char *out, size_t len) {
    RtcTime t = {0};
    HAL_StatusTypeDef ret = DS1307_ReadTime(hi2c, &t);
    if (ret == HAL_OK) {
        snprintf(out, len, "%02d:%02d:%02d", t.hours, t.minutes, t.seconds);
    }
    return ret;
}

/* Записати повний час (запускає годинник, скидаючи біт CH) */
static inline HAL_StatusTypeDef DS1307_SetTime(I2C_HandleTypeDef *hi2c, const RtcTime *t) {
    uint8_t raw[DS1307_TIME_REGS] = {
        [DS1307_REG_SECONDS] = DS1307_DecToBcd(t->seconds & DS1307_MASK_SECONDS),  /* CH=0 -> годинник запущено */
        [DS1307_REG_MINUTES] = DS1307_DecToBcd(t->minutes),
        [DS1307_REG_HOURS]   = DS1307_DecToBcd(t->hours),   /* bit6=0 -> 24-годинний формат */
        [DS1307_REG_DAY]     = DS1307_DecToBcd(t->day),
        [DS1307_REG_DATE]    = DS1307_DecToBcd(t->date),
        [DS1307_REG_MONTH]   = DS1307_DecToBcd(t->month),
        [DS1307_REG_YEAR]    = DS1307_DecToBcd(t->year),
    };
    return HAL_I2C_Mem_Write(hi2c, DS1307_ADDR, DS1307_REG_SECONDS, I2C_MEMADD_SIZE_8BIT,
                             raw, sizeof(raw), DS1307_I2C_TIMEOUT);
}

#endif /* DS1307_H */
