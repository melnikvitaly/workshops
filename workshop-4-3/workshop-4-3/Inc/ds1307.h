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

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;   /* 24-годинний формат */
    uint8_t day;     /* день тижня 1..7 */
    uint8_t date;    /* число 1..31 */
    uint8_t month;   /* 1..12 */
    uint8_t year;    /* 0..99 (роки 2000..2099) */
} RtcTime;

static inline uint8_t DS1307_BcdToDec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t DS1307_DecToBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* Зчитати лише секунди (зручно для тесту/тіку годинника) */
static inline HAL_StatusTypeDef DS1307_ReadSeconds(I2C_HandleTypeDef *hi2c, uint8_t *seconds) {
    uint8_t raw = 0;
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(hi2c, DS1307_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, &raw, 1, 100);
    if (ret == HAL_OK) {
        *seconds = DS1307_BcdToDec(raw & 0x7F);  /* відкидаємо біт CH */
    }
    return ret;
}

/* Зчитати повний час */
static inline HAL_StatusTypeDef DS1307_ReadTime(I2C_HandleTypeDef *hi2c, RtcTime *out) {
    uint8_t raw[7] = {0};
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(hi2c, DS1307_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, raw, sizeof(raw), 100);
    if (ret != HAL_OK) {
        return ret;
    }
    out->seconds = DS1307_BcdToDec(raw[0] & 0x7F);
    out->minutes = DS1307_BcdToDec(raw[1] & 0x7F);
    out->hours   = DS1307_BcdToDec(raw[2] & 0x3F);  /* 24-годинний формат */
    out->day     = DS1307_BcdToDec(raw[3] & 0x07);
    out->date    = DS1307_BcdToDec(raw[4] & 0x3F);
    out->month   = DS1307_BcdToDec(raw[5] & 0x1F);
    out->year    = DS1307_BcdToDec(raw[6]);
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
    uint8_t raw[7] = {
        DS1307_DecToBcd(t->seconds & 0x7F),  /* CH=0 -> годинник запущено */
        DS1307_DecToBcd(t->minutes),
        DS1307_DecToBcd(t->hours),           /* bit6=0 -> 24-годинний формат */
        DS1307_DecToBcd(t->day),
        DS1307_DecToBcd(t->date),
        DS1307_DecToBcd(t->month),
        DS1307_DecToBcd(t->year),
    };
    return HAL_I2C_Mem_Write(hi2c, DS1307_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, raw, sizeof(raw), 100);
}

#endif /* DS1307_H */
