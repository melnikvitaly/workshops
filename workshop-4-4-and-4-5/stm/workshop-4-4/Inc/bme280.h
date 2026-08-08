#ifndef BME280_H
#define BME280_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ============================================================================
 *  BME280 — температура / вологість / тиск (I2C) — header-only, STM32 HAL
 * ============================================================================
 *  Сидить на тій самій шині I2C1, що RTC (0x68), EEPROM (0x50) і OLED (0x3C).
 *  Адреса залежить від піна SDO: 0x76 (SDO->GND, так на більшості модулів
 *  GY-BM E/P) або 0x77 (SDO->VCC). BME280_Init пробує обидві, тож модуль не
 *  треба перепаювати під конкретну плату.
 *
 *  ⚠️ Є два майже однакових чипи: BME280 (id 0x60) вміє вологість, BMP280
 *  (id 0x58) — ні. Вони pin-сумісні й на вигляд однакові. BME280_Init звіряє
 *  id і не піднімається на BMP280: інакше вологість читалась би як стале
 *  сміття, і це виглядало б як "датчик працює, просто вологість дивна".
 *
 *  Карта регістрів (те, що використовуємо):
 *    0x88..0xA1  калібрування dig_T1..T3, dig_P1..P9, dig_H1
 *    0xD0        chip id (0x60 = BME280)
 *    0xE0        reset (запис 0xB6)
 *    0xE1..0xE7  калібрування dig_H2..H6
 *    0xF2        ctrl_hum   (osrs_h) — діє ЛИШЕ після запису в 0xF4
 *    0xF4        ctrl_meas  (osrs_t | osrs_p | mode)
 *    0xF5        config     (t_sb | filter)
 *    0xF7..0xFE  дані: тиск(3) + температура(3) + вологість(2)
 *
 *  Режим — NORMAL, а не FORCED. У forced треба після кожного запуску чекати,
 *  доки вимір завершиться (опитувати статус або спати ~10 мс), а головний цикл
 *  тут нічого не блокує. У normal чип міряє сам за таймером standby, і читання
 *  зводиться до одного обміну по I2C: беремо останній готовий результат.
 *  Standby 500 мс проти нашого періоду опитування 1 с -> кожне читання свіже.
 *
 *  Компенсація — цілочисельна, з даташита Bosch (розділ 4.2.3, "compensation
 *  formulas"). float тут не варіант: FPU в цій збірці не ввімкнено, а
 *  фіксована точка дає точно ті самі числа за цілі такти. Одиниці, які
 *  повертають формули, різні (0.01 °C, Q24.8 Па, Q22.10 %RH) — BME280_Read
 *  зводить їх до одного вигляду, зручного і для екрана, і для SPI-пакета.
 *
 *  I2C-периферію (MX_I2C1_Init) треба ініціалізувати ЗОВНІ до використання.
 * ============================================================================ */

/* --- Адреси на шині (7-бітні, до зсуву для HAL) --- */
#define BME280_ADDR_PRIMARY   0x76  /* SDO -> GND (типово для GY-модулів) */
#define BME280_ADDR_SECONDARY 0x77  /* SDO -> VCC */

/* --- Регістри --- */
#define BME280_REG_CALIB_TP   0x88  /* блок калібрування T/P + dig_H1 */
#define BME280_REG_CHIP_ID    0xD0
#define BME280_REG_RESET      0xE0
#define BME280_REG_CALIB_H    0xE1  /* блок калібрування вологості */
#define BME280_REG_CTRL_HUM   0xF2
#define BME280_REG_STATUS     0xF3
#define BME280_REG_CTRL_MEAS  0xF4
#define BME280_REG_CONFIG     0xF5
#define BME280_REG_DATA       0xF7  /* початок блоку вимірів */

#define BME280_CHIP_ID        0x60  /* саме BME280; 0x58 = BMP280 (без вологості) */
#define BME280_RESET_WORD     0xB6  /* єдине значення, яке чип приймає як скидання */

/* --- Розміри блоків одного обміну --- */
#define BME280_CALIB_TP_LEN   26    /* 0x88..0xA1 */
#define BME280_CALIB_H_LEN    7     /* 0xE1..0xE7 */
#define BME280_DATA_LEN       8     /* 0xF7..0xFE: 3 тиск + 3 темп + 2 вологість */

/* --- ctrl_meas / ctrl_hum / config: розкладка бітів --- */
#define BME280_OSRS_X1        0x01  /* однократне передискретизування — вистачає */
#define BME280_OSRS_T_SHIFT   5
#define BME280_OSRS_P_SHIFT   2
#define BME280_MODE_SLEEP     0x00
#define BME280_MODE_FORCED    0x01
#define BME280_MODE_NORMAL    0x03
#define BME280_TSB_500MS      0x04  /* standby 500 мс */
#define BME280_TSB_SHIFT      5
#define BME280_FILTER_OFF     0x00
#define BME280_FILTER_SHIFT   2

/* Дедлайн одного обміну (мс): найдовший — 26 байтів калібрування на 100 кГц. */
#define BME280_I2C_TIMEOUT    100

/* Після скидання чип перечитує калібрування з NVM; поки триває це копіювання,
 * регістри 0x88.. читаються нулями. Даташит дає ~2 мс на старт. */
#define BME280_RESET_DELAY_MS 5

/* Скільки разів пінгувати адресу перед тим, як вважати її порожньою. */
#define BME280_PROBE_TRIALS   2
#define BME280_PROBE_TIMEOUT  10

/* --- Одиниці, у яких BME280_Read віддає результат --- */
#define BME280_TEMP_SCALE       100   /* tempC100     -> °C  */
#define BME280_HUMIDITY_SCALE   100   /* humidity100  -> %RH */
/* Тиск формула віддає у форматі Q24.8 (ціла частина = Па), вологість — Q22.10. */
#define BME280_PRESSURE_Q       256
#define BME280_HUMIDITY_Q       1024

/* Сирі 20-бітні виміри T/P пакуються як msb<<12 | lsb<<4 | xlsb>>4. */
#define BME280_ADC_MSB_SHIFT    12
#define BME280_ADC_LSB_SHIFT    4
#define BME280_ADC_XLSB_SHIFT   4

/* Коефіцієнти калібрування, зчитані з чипа один раз при ініціалізації. Імена —
 * як у даташиті, щоб формули нижче можна було звірити з ним рядок у рядок. */
typedef struct {
    uint16_t T1;
    int16_t  T2, T3;
    uint16_t P1;
    int16_t  P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1;
    int16_t  H2;
    uint8_t  H3;
    int16_t  H4, H5;
    int8_t   H6;
} Bme280Calib;

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t            addr;    /* зсунута 8-бітна адреса для HAL */
    uint8_t            ready;   /* 1 після успішної BME280_Init */
    Bme280Calib        calib;
    /* t_fine переносить температурну поправку з формули температури у формули
     * тиску й вологості — тому температуру ЗАВЖДИ треба компенсувати першою
     * (так само в даташиті). Тримаємо його тут, а не в статичній змінній, щоб
     * два датчики на одній шині не псували одне одному результат. */
    int32_t            tFine;
} Bme280;

/* Результат одного вимірювання, зведений до цілих одиниць. Точно ті самі
 * масштаби, що йдуть у SPI-пакет (див. protocol/telemetry_packet.h), тож між
 * датчиком і дротом немає жодного перетворення, яке можна переплутати. */
typedef struct {
    int16_t  tempC100;     /* температура, °C * 100 */
    uint16_t humidity100;  /* вологість, %RH * 100 (0..10000) */
    uint32_t pressurePa;   /* тиск у паскалях */
} Bme280Reading;

/* --- Низькорівневий обмін --- */
static inline HAL_StatusTypeDef BME280_ReadRegs(Bme280 *d, uint8_t reg, uint8_t *buf, uint16_t len) {
    return HAL_I2C_Mem_Read(d->hi2c, d->addr, reg, I2C_MEMADD_SIZE_8BIT, buf, len, BME280_I2C_TIMEOUT);
}

static inline HAL_StatusTypeDef BME280_WriteReg(Bme280 *d, uint8_t reg, uint8_t value) {
    return HAL_I2C_Mem_Write(d->hi2c, d->addr, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, BME280_I2C_TIMEOUT);
}

/* Little-endian пари байтів у калібрувальному блоці. */
static inline uint16_t BME280_U16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[1] << 8 | p[0]);
}
static inline int16_t BME280_S16(const uint8_t *p) {
    return (int16_t)BME280_U16(p);
}

/* Розібрати обидва калібрувальні блоки. dig_H4/H5 — 12-бітні й лежать
 * "внахлест" у трьох байтах 0xE4..0xE6: H4 = E4[7:0]<<4 | E5[3:0],
 * H5 = E6[7:0]<<4 | E5[7:4]. Спільний байт E5 — саме те місце, де в самописних
 * драйверах найчастіше з'їжджає вологість. */
static inline void BME280_ParseCalib(Bme280Calib *c, const uint8_t *tp, const uint8_t *h) {
    c->T1 = BME280_U16(&tp[0]);
    c->T2 = BME280_S16(&tp[2]);
    c->T3 = BME280_S16(&tp[4]);
    c->P1 = BME280_U16(&tp[6]);
    c->P2 = BME280_S16(&tp[8]);
    c->P3 = BME280_S16(&tp[10]);
    c->P4 = BME280_S16(&tp[12]);
    c->P5 = BME280_S16(&tp[14]);
    c->P6 = BME280_S16(&tp[16]);
    c->P7 = BME280_S16(&tp[18]);
    c->P8 = BME280_S16(&tp[20]);
    c->P9 = BME280_S16(&tp[22]);
    /* tp[24] = 0xA0, не використовується; tp[25] = 0xA1 = dig_H1 */
    c->H1 = tp[25];

    c->H2 = BME280_S16(&h[0]);              /* 0xE1..0xE2 */
    c->H3 = h[2];                           /* 0xE3 */
    c->H4 = (int16_t)((int16_t)((int8_t)h[3]) * 16 + (h[4] & 0x0F));       /* 0xE4, 0xE5[3:0] */
    c->H5 = (int16_t)((int16_t)((int8_t)h[5]) * 16 + ((h[4] >> 4) & 0x0F)); /* 0xE6, 0xE5[7:4] */
    c->H6 = (int8_t)h[6];                   /* 0xE7 */
}

/* --- Компенсація (цілочисельні формули з даташита Bosch) --- */

/* Температура -> °C * 100. Побічно заповнює d->tFine для формул нижче. */
static inline int32_t BME280_CompensateT(Bme280 *d, int32_t adcT) {
    const Bme280Calib *c = &d->calib;
    int32_t var1 = ((((adcT >> 3) - ((int32_t)c->T1 << 1))) * ((int32_t)c->T2)) >> 11;
    int32_t var2 = (((((adcT >> 4) - ((int32_t)c->T1)) * ((adcT >> 4) - ((int32_t)c->T1))) >> 12)
                    * ((int32_t)c->T3)) >> 14;
    d->tFine = var1 + var2;
    return (d->tFine * 5 + 128) >> 8;
}

/* Тиск -> Q24.8 паскалів (ділити на 256). 64-бітна версія формули: 32-бітна з
 * даташита втрачає точність біля країв діапазону, а такт тут не критичний —
 * одне множення раз на секунду. */
static inline uint32_t BME280_CompensateP(const Bme280 *d, int32_t adcP) {
    const Bme280Calib *c = &d->calib;
    int64_t var1 = ((int64_t)d->tFine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)c->P6;
    int64_t p;

    var2 = var2 + ((var1 * (int64_t)c->P5) << 17);
    var2 = var2 + (((int64_t)c->P4) << 35);
    var1 = ((var1 * var1 * (int64_t)c->P3) >> 8) + ((var1 * (int64_t)c->P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)c->P1) >> 33;
    if (var1 == 0) {
        return 0;   /* ділення на нуль: калібрування не зчиталось */
    }
    p = 1048576 - adcP;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)c->P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)c->P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)c->P7) << 4);
    return (uint32_t)p;
}

/* Вологість -> Q22.10 %RH (ділити на 1024). Верхня межа 419430400 — це рівно
 * 100 %RH у цьому форматі; формула може вилізти за неї на краях калібрування. */
static inline uint32_t BME280_CompensateH(const Bme280 *d, int32_t adcH) {
    const Bme280Calib *c = &d->calib;
    int32_t v = d->tFine - ((int32_t)76800);

    v = (((((adcH << 14) - (((int32_t)c->H4) << 20) - (((int32_t)c->H5) * v))
           + ((int32_t)16384)) >> 15)
         * (((((((v * ((int32_t)c->H6)) >> 10)
                * (((v * ((int32_t)c->H3)) >> 11) + ((int32_t)32768))) >> 10)
              + ((int32_t)2097152)) * ((int32_t)c->H2) + 8192) >> 14));
    v = (v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)c->H1)) >> 4));
    v = (v < 0 ? 0 : v);
    v = (v > 419430400 ? 419430400 : v);
    return (uint32_t)(v >> 12);
}

/* --- Публічний API --- */

/* Заповнити структуру перед BME280_Init. */
static inline void BME280_Setup(Bme280 *d, I2C_HandleTypeDef *hi2c) {
    d->hi2c  = hi2c;
    d->addr  = (uint8_t)(BME280_ADDR_PRIMARY << 1);
    d->ready = 0;
    d->tFine = 0;
}

/* Спробувати підняти датчик за конкретною адресою: звірити chip id, скинути,
 * зчитати калібрування і запустити NORMAL-режим. */
static inline HAL_StatusTypeDef BME280_InitAt(Bme280 *d, uint8_t addr7) {
    uint8_t tp[BME280_CALIB_TP_LEN];
    uint8_t h[BME280_CALIB_H_LEN];
    uint8_t id = 0;
    HAL_StatusTypeDef ret;

    d->addr  = (uint8_t)(addr7 << 1);
    d->ready = 0;

    ret = BME280_ReadRegs(d, BME280_REG_CHIP_ID, &id, sizeof(id));
    if (ret != HAL_OK) {
        return ret;
    }
    if (id != BME280_CHIP_ID) {
        return HAL_ERROR;   /* щось відповіло, але це не BME280 (напр. BMP280) */
    }

    ret = BME280_WriteReg(d, BME280_REG_RESET, BME280_RESET_WORD);
    if (ret != HAL_OK) {
        return ret;
    }
    HAL_Delay(BME280_RESET_DELAY_MS);   /* один раз на старті — головний цикл ще не крутиться */

    ret = BME280_ReadRegs(d, BME280_REG_CALIB_TP, tp, sizeof(tp));
    if (ret != HAL_OK) {
        return ret;
    }
    ret = BME280_ReadRegs(d, BME280_REG_CALIB_H, h, sizeof(h));
    if (ret != HAL_OK) {
        return ret;
    }
    BME280_ParseCalib(&d->calib, tp, h);

    /* ctrl_hum пишемо ПЕРШИМ: чип застосовує його лише при наступному записі в
     * ctrl_meas. Зробити навпаки — і вологість лишиться вимкненою (0x8000),
     * тобто читатиметься як стале число. */
    ret = BME280_WriteReg(d, BME280_REG_CTRL_HUM, BME280_OSRS_X1);
    if (ret != HAL_OK) {
        return ret;
    }
    ret = BME280_WriteReg(d, BME280_REG_CONFIG,
                          (uint8_t)((BME280_TSB_500MS << BME280_TSB_SHIFT)
                                    | (BME280_FILTER_OFF << BME280_FILTER_SHIFT)));
    if (ret != HAL_OK) {
        return ret;
    }
    ret = BME280_WriteReg(d, BME280_REG_CTRL_MEAS,
                          (uint8_t)((BME280_OSRS_X1 << BME280_OSRS_T_SHIFT)
                                    | (BME280_OSRS_X1 << BME280_OSRS_P_SHIFT)
                                    | BME280_MODE_NORMAL));
    if (ret != HAL_OK) {
        return ret;
    }

    d->ready = 1;
    return HAL_OK;
}

/* Підняти датчик, пробуючи обидві можливі адреси (0x76, потім 0x77).
 * Невдача не фатальна: викликач може спробувати ще раз пізніше (гаряче
 * підключення теж працює) — так само, як це робить OLED у display_ui.c. */
static inline HAL_StatusTypeDef BME280_Init(Bme280 *d) {
    static const uint8_t addrs[] = { BME280_ADDR_PRIMARY, BME280_ADDR_SECONDARY };
    HAL_StatusTypeDef last = HAL_ERROR;
    for (unsigned i = 0; i < sizeof(addrs) / sizeof(addrs[0]); ++i) {
        /* Спершу дешевий пінг: без нього кожна відсутня адреса коштувала б
         * повного BME280_I2C_TIMEOUT у читанні chip id. */
        if (HAL_I2C_IsDeviceReady(d->hi2c, (uint16_t)(addrs[i] << 1),
                                  BME280_PROBE_TRIALS, BME280_PROBE_TIMEOUT) != HAL_OK) {
            continue;
        }
        last = BME280_InitAt(d, addrs[i]);
        if (last == HAL_OK) {
            return HAL_OK;
        }
    }
    return last;
}

/* Зчитати останній готовий вимір одним обміном (8 байтів, 0xF7..0xFE).
 * Саме одним: тиск, температура і вологість беруться з одного знімка, тож
 * температурна поправка t_fine гарантовано належить тому самому виміру.
 * out лишається без змін, якщо читання не вдалося. */
static inline HAL_StatusTypeDef BME280_Read(Bme280 *d, Bme280Reading *out) {
    uint8_t raw[BME280_DATA_LEN];
    int32_t adcP, adcT, adcH;
    HAL_StatusTypeDef ret;

    if (!d->ready) {
        return HAL_ERROR;
    }
    ret = BME280_ReadRegs(d, BME280_REG_DATA, raw, sizeof(raw));
    if (ret != HAL_OK) {
        d->ready = 0;   /* датчик зник з шини — хай викликач переініціалізує */
        return ret;
    }

    adcP = (int32_t)(((uint32_t)raw[0] << BME280_ADC_MSB_SHIFT)
                     | ((uint32_t)raw[1] << BME280_ADC_LSB_SHIFT)
                     | ((uint32_t)raw[2] >> BME280_ADC_XLSB_SHIFT));
    adcT = (int32_t)(((uint32_t)raw[3] << BME280_ADC_MSB_SHIFT)
                     | ((uint32_t)raw[4] << BME280_ADC_LSB_SHIFT)
                     | ((uint32_t)raw[5] >> BME280_ADC_XLSB_SHIFT));
    adcH = (int32_t)(((uint32_t)raw[6] << 8) | (uint32_t)raw[7]);

    /* Порядок обов'язковий: температура заповнює t_fine, від якого залежать
     * решта дві формули. */
    out->tempC100    = (int16_t)BME280_CompensateT(d, adcT);
    out->pressurePa  = BME280_CompensateP(d, adcP) / BME280_PRESSURE_Q;
    out->humidity100 = (uint16_t)((BME280_CompensateH(d, adcH) * BME280_HUMIDITY_SCALE)
                                  / BME280_HUMIDITY_Q);
    return HAL_OK;
}

#endif /* BME280_H */
