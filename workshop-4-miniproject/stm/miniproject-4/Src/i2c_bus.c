#include "i2c_bus.h"

// I2C1 сидить на PB6 (SCL) і PB7 (SDA), AF4, відкритий стік (див. HAL_I2C_MspInit).
#define I2C_SCL_PIN   GPIO_PIN_6
#define I2C_SDA_PIN   GPIO_PIN_7
#define I2C_GPIO_PORT GPIOB

// Скільки тактів видати: 8 біт даних + такт ACK — рівно стільки треба, щоб раб
// дотиснув найдовший недописаний байт.
#define I2C_RECOVERY_CLOCKS (8 + 1)

// Скільки порожніх обертів робить пауза на півтакту (~10 мкс на 16 МГц).
#define I2C_BIT_DELAY_NOPS 20

// Груба затримка на півтакту — це аварійне звільнення шини, а не робочий обмін,
// тож точність не потрібна, аби лиш було не швидше 100 кГц.
static void i2c_bit_delay(void)
{
    for (volatile int i = 0; i < I2C_BIT_DELAY_NOPS; ++i) {
        __NOP();
    }
}

// Звільнити шину, яку тримає завислий раб.
//
// Якщо обмін обірвався посеред байта (таймаут без STOP), раб лишається чекати
// решту тактів і тримає SDA притиснутою до землі. Скидання МК тут не допомагає
// НІЯК: раб не бачив скидання і не має про нього поняття — саме тому дисплей
// оживає лише після зняття живлення. Так само не рятує і SWRST у HAL_I2C_DeInit:
// він скидає периферію STM32, а не раба.
//
// Єдиний спосіб з боку майстра — доклацати за нього: перевести SCL у звичайний
// GPIO і видати до 9 тактів (байт + ACK), доки SDA не відпустять, а тоді вручну
// зробити STOP (SDA піднімається, поки SCL угорі).
void I2CBus_Release(void)
{
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Обидві лінії — GPIO з відкритим стоком, відпущені (лог. 1). Внутрішня
    // підтяжка слабка (~40 кОм) і зовнішніх резисторів не замінює, але не заважає.
    gi.Mode  = GPIO_MODE_OUTPUT_OD;
    gi.Pull  = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    gi.Pin   = I2C_SCL_PIN | I2C_SDA_PIN;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gi);
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN | I2C_SDA_PIN, GPIO_PIN_SET);
    i2c_bit_delay();

    // Рівно I2C_RECOVERY_CLOCKS тактів вистачає, щоб раб дотиснув байт.
    for (int i = 0; i < I2C_RECOVERY_CLOCKS; ++i) {
        if (HAL_GPIO_ReadPin(I2C_GPIO_PORT, I2C_SDA_PIN) == GPIO_PIN_SET) {
            break;   // SDA відпустили — раб вийшов зі свого байта
        }
        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
        i2c_bit_delay();
        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
        i2c_bit_delay();
    }

    // Ручний STOP, щоб усі раби повернулися в idle.
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
    i2c_bit_delay();
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    i2c_bit_delay();
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
    i2c_bit_delay();
}

// Підняти шину після зірваного обміну: SWRST у DeInit чистить свій бік,
// 9 тактів — чужий, HAL_I2C_Init повертає піни в AF4. Коштує дешево (кілька
// сотень мікросекунд), тож може викликатись раз на кадр і заразом дає гарячу
// заміну пристроїв на шині.
HAL_StatusTypeDef I2CBus_Recover(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL) {
        return HAL_ERROR;
    }

    HAL_I2C_DeInit(hi2c);   // SWRST: чистимо свій бік і відпускаємо піни
    I2CBus_Release();       // 9 тактів: чистимо чужий бік
    return HAL_I2C_Init(hi2c);   // назад у AF4 з тими самими параметрами
}
