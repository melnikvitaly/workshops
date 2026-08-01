#ifndef ADC_H
#define ADC_H

#include "stm32f4xx_hal.h"

/* ============================================================================
 *  ADC helper (фоторезистор) — header-only, STM32 HAL
 * ============================================================================
 *  Одиночне вимірювання на каналі, налаштованому в MX_ADC1_Init.
 *  Роздільність 12 біт -> сире значення 0..4095.
 *  АЦП-периферію (MX_ADC1_Init) треба ініціалізувати ЗОВНІ до використання.
 * ============================================================================ */

#define ADC_MAX_RAW      4095  /* максимум для 12-бітної роздільності */
#define ADC_PERCENT_FULL 100   /* повна шкала у відсотках */
#define ADC_POLL_TIMEOUT 10    /* дедлайн одного перетворення, мс */

/* Одне вимірювання -> сире 12-бітне значення (0..4095).
 * Повертає HAL_OK, а результат кладе у *out. */
static inline HAL_StatusTypeDef ADC_ReadRaw(ADC_HandleTypeDef *hadc, uint32_t *out) {
    HAL_ADC_Start(hadc);
    HAL_StatusTypeDef ret = HAL_ADC_PollForConversion(hadc, ADC_POLL_TIMEOUT);
    if (ret == HAL_OK) {
        *out = HAL_ADC_GetValue(hadc);
    }
    HAL_ADC_Stop(hadc);
    return ret;
}

/* Одне вимірювання -> відсотки освітлення (0..100).
 * Повертає HAL_OK, а результат кладе у *percent. */
static inline HAL_StatusTypeDef ADC_ReadPercent(ADC_HandleTypeDef *hadc, uint8_t *percent) {
    uint32_t raw = 0;
    HAL_StatusTypeDef ret = ADC_ReadRaw(hadc, &raw);
    if (ret == HAL_OK) {
        *percent = (uint8_t)((raw * ADC_PERCENT_FULL) / ADC_MAX_RAW);
    }
    return ret;
}

#endif /* ADC_H */
