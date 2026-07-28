/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

#include "config.h"       // усі налаштування прошивки в одному місці

// Драйвери I2C-пристроїв (порт з workshop-4-2, адаптований під STM32 HAL)
#include "ds1307.h"        // RTC годинник реального часу (0x68)
#include "ssd1306.h"       // OLED дисплей (0x3C)
#include "text_renderer.h" // малює годинник/адреси шрифтом 5x7
#include "i2c_scanner.h"   // сканер шини I2C
#include "eeprom.h"        // журнал у зовнішній EEPROM (0x50)
#include "adc.h"           // вимірювання освітлення через АЦП
#include "log_emission.h"  // SPI-slave: віддає Data Logs ESP32-майстру
#include "sensor_stream.h" // ADC(DMA)+RTC -> потік Data Logs
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// Налаштування (адреси, розкладка, періоди) винесено в config.h
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;

TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */
// Змінні для керування логуванням
uint32_t last_log_time = 0;
uint16_t current_address = 0;

// Об'єкти та стан для дисплея / годинника / сканера
Ssd1306  oled;                 // готовність дисплея зберігає сам драйвер (oled.ready)
uint32_t last_scan_time = 0;   // коли востаннє сканували шину
uint32_t last_frame_time = 0;  // коли востаннє оновлювали кадр на екрані

// Скільки разів зривався обмін з дисплеєм по I2C (таймаут/NACK/зайнята шина).
// Показуємо на екрані як "E<n>": нуль, що тримається, означає, що запасу за
// часом вистачає; зростання — що шина на межі. Обмежене згори, щоб рядок
// лічильників не розповзався по ширині.
#define OLED_FAIL_MAX 9999u
uint32_t oled_fail_count = 0;

// Рядки одного кадру (показуємо на екрані щокадру). Стан I2C-шини показуємо у
// hex — так само, як його друкує сканер.
typedef struct {
    char devices[DEVICES_STR_LEN];  // "0x3C 0x68 ..." — результат сканування
    char clock[CLOCK_STR_LEN];      // "HH:MM:SS" — час з RTC
    char light[LIGHT_STR_LEN];      // "L<АЦП>" — сире значення освітлення
    char rates[RATES_STR_LEN];      // "P<пакетів/с> E<збоїв I2C> <кбіт/с>kb"
} FrameData;

FrameData frame = { "0x00", "--:--:--", "L----", "P- E0 -kb" };  // до першого виміру
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// Функції роботи з EEPROM винесено в окремий драйвер (Inc/eeprom.h)

// Раз на LOG_INTERVAL_MS виміряти освітлення й дописати його в EEPROM.
// За реальним часом (HAL_GetTick), а не за лічильником ітерацій.
static void poll_light_log(void) {
    if (last_log_time != 0 && HAL_GetTick() - last_log_time < LOG_INTERVAL_MS) {
        return;
    }
    last_log_time = HAL_GetTick();

    // Потік у SPI (записи "LGHT"/"TIME") веде sensor_stream; тут лише архівуємо
    // останнє усереднене значення освітлення в EEPROM раз на LOG_INTERVAL_MS.
    uint8_t light_percent = SensorStream_LatestLightPercent();
    if (current_address <= EEPROM_DATA_END) {
        // Просуваємо (і зберігаємо) вказівник лише якщо байт справді записався:
        // якщо просто ігнорувати статус, зірваний обмін (NACK/таймаут) мовчки
        // лишає непрописаний байт, а вказівник вже стоїть за ним — журнал
        // назавжди має дірку. Провал тут просто повторить спробу на цю саму
        // адресу наступного разу.
        if (EEPROM_WriteByte(&hi2c1, current_address, light_percent) == HAL_OK) {
            current_address++;                                  // зсуваємо вказівник
            EEPROM_SaveNextAddress(&hi2c1, current_address);    // і зберігаємо його
        }
    }
}

// Просканувати шину не частіше, ніж раз на SCAN_PERIOD_MS, і оновити
// рядок frame.devices (перший скан спрацьовує одразу).
static void poll_i2c_scan(void) {
    if (last_scan_time != 0 && HAL_GetTick() - last_scan_time < SCAN_PERIOD_MS) {
        return;
    }
    last_scan_time = HAL_GetTick();
    I2CScanner_ScanToString(&hi2c1, frame.devices, sizeof(frame.devices), MAX_DEVICES_SHOWN);
}

// I2C1 сидить на PB6 (SCL) і PB7 (SDA), AF4, відкритий стік (див. HAL_I2C_MspInit).
#define I2C_SCL_PIN   GPIO_PIN_6
#define I2C_SDA_PIN   GPIO_PIN_7
#define I2C_GPIO_PORT GPIOB

// Груба затримка на півтакту (~10 мкс на 16 МГц) — це аварійне звільнення шини,
// а не робочий обмін, тож точність не потрібна, аби лиш було не швидше 100 кГц.
static void i2c_bit_delay(void) {
    for (volatile int i = 0; i < 20; ++i) {
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
static void i2c_bus_release(void) {
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

    // Рівно 9 тактів вистачає, щоб раб дотиснув найдовший недописаний байт.
    for (int i = 0; i < 9; ++i) {
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

// Підняти шину I2C1 після зірваного обміну. Якщо HAL_I2C_Mem_Write вийшов по
// таймауту, він НЕ виставляє STOP (це робиться лише при NACK), тож ламається
// одразу з двох боків: периферія STM32 лишається з піднятим BUSY, а раб — з
// притиснутою SDA. Лікуємо обидва: SWRST у DeInit чистить свій бік, 9 тактів —
// чужий. Викликається не частіше разу на кадр, тож коштує дешево і заразом дає
// гарячу заміну дисплея.
static void i2c_bus_recover(void) {
    HAL_I2C_DeInit(&hi2c1);   // SWRST: чистимо свій бік і відпускаємо піни
    i2c_bus_release();        // 9 тактів: чистимо чужий бік
    MX_I2C1_Init();           // назад у AF4
    SSD1306_Init(&oled);      // сам виставить oled.ready за успіху
}

// Намалювати один кадр: годинник + адреси пристроїв + освітлення + темпи SPI.
// Розкладку (Y та масштаб кожного рядка) задано в config.h.
static void render_frame(void) {
    SSD1306_Clear(&oled, 0x00);   // чистимо буфер: далі домальовуємо всі рядки

    Text_DrawTextCentered(&oled, CLOCK_Y, frame.clock, CLOCK_SCALE);        // годинник зверху
    Text_DrawTextCentered(&oled, DEVICES_Y, frame.devices, DEVICES_SCALE);  // адреси під ним
    Text_DrawTextCentered(&oled, LIGHT_Y, frame.light, LIGHT_SCALE);        // освітлення (АЦП)
    Text_DrawTextCentered(&oled, RATES_Y, frame.rates, RATES_SCALE);        // темпи SPI-потоку

    // Раніше результат ігнорувався: один зірваний обмін — і картинка застигала
    // без жодних ознак. Тепер помічаємо дисплей як неготовий, і наступний кадр
    // підніме шину (див. poll_frame).
    if (SSD1306_Flush(&oled) != HAL_OK) {
        oled.ready = 0;
        if (oled_fail_count < OLED_FAIL_MAX) {
            ++oled_fail_count;
        }
    }
}

// Раз на FRAME_PERIOD_MS оновити час з RTC і перемалювати кадр.
static void poll_frame(void) {
    if (last_frame_time != 0 && HAL_GetTick() - last_frame_time < FRAME_PERIOD_MS) {
        return;
    }
    last_frame_time = HAL_GetTick();

    // frame.clock лишається без змін, якщо RTC не відповів (показуємо старий час).
    DS1307_ReadTimeString(&hi2c1, frame.clock, sizeof(frame.clock));

    // L = сире значення АЦП світла (0..4095, те саме, що йде в пакет "LGHT").
    snprintf(frame.light, sizeof(frame.light), "L%u",
             (unsigned)SensorStream_LatestLightRaw());

    // P = скільки пакетів поставлено в потік за останню секунду,
    // E = скільки разів зривався обмін з дисплеєм по I2C від старту,
    // <n>kb = темп SPI у кілобітах/с.
    //
    // Кілобіти зручні тим, що число прямо звіряється з тактовою частотою SPI:
    // на 1 МГц майстер вичитує ~999kb, тож видно, що лінк іде на повній швидкості.
    // Крок дискретизації — один оберт кільця (2048 Б = ~16 кбіт/с), бо байти
    // рахуються обертами DMA (див. stream_bytes_total у log_emission.c).
    LogEmitStats st;
    LogEmission_GetStats(&st);
    unsigned kbits = (unsigned)((st.bytesPerSec * 8u + 500u) / 1000u);
    snprintf(frame.rates, sizeof(frame.rates), "P%u E%u %ukb",
             (unsigned)st.packetsPerSec,
             (unsigned)oled_fail_count,
             kbits);

    if (oled.ready) {
        render_frame();
    } else {
        // Дисплей не піднявся на старті або обмін зірвався — пробуємо раз на
        // кадр, доки не вийде. Без цього oled.ready лишався б 0 назавжди.
        i2c_bus_recover();
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  // Шина могла лишитись захопленою ще з минулого сеансу: якщо обмін обірвався
  // посеред байта, раб тримає SDA і скидання МК він не бачив — саме тому плата
  // піднімалась мертвою і оживала лише після зняття живлення з дисплея. Тому
  // ПЕРШИМ ділом доклацуємо шину, інакше і EEPROM, і дисплей нижче отримають
  // HAL_BUSY і не піднімуться.
  i2c_bus_release();
  MX_I2C1_Init();   // повертаємо PB6/PB7 з ручного GPIO назад у AF4

  // Зчитуємо адресу з пам'яті під час старту пристрою
  current_address = EEPROM_GetNextAddress(&hi2c1);

  // Ініціалізація OLED-дисплея на тій самій шині I2C1 (параметри — у config.h).
  // Готовність дисплея далі зберігає сам драйвер (oled.ready).
  SSD1306_Setup(&oled, &hi2c1, OLED_ADDR, OLED_WIDTH, OLED_HEIGHT);
  SSD1306_Init(&oled);

  // SPI1 як slave: віддає накопичені Data Logs ESP32-майстру (PA4..PA7).
  LogEmission_Init();

  // Потокове зчитування сенсорів (ADC світла через TIM2+DMA, час з RTC) у
  // той самий SPI-потік. Має йти після MX_ADC1_Init/MX_I2C1_Init.
  // У режимі LOGEMIT_DEBUG_SPI сенсорний потік вимкнено: інакше реальні пакети
  // "LGHT"/"TIME" переписують налагоджувальну "пилку" 0x00..0xFF у SPI-кільці і
  // рамп зникає. Так MISO залишається стабільним сигналом для перевірки ліній.
  // Прибрати ці #if-guard'и для звичайної роботи (LOGEMIT_DEBUG_SPI = 0).
#if !LOGEMIT_DEBUG_SPI
  SensorStream_Init();
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // Кожен помічник неблокуюче перевіряє свій таймер (HAL_GetTick) і
    // виконує роботу лише коли настав його період — деталі у USER CODE 0.
#if !LOGEMIT_DEBUG_SPI
    SensorStream_Poll();  // ADC(DMA)+RTC -> записи "LGHT"/"TIME" у SPI-потік
#endif
    LogEmission_ActivityPoll();  // блимання світлодіода PC13 при SPI-обміні з майстром
    poll_light_log();  // архів освітлення в EEPROM (раз на годину)
    poll_i2c_scan();   // сканування шини I2C (кожні 10 с)
    poll_frame();      // оновлення кадру: кіт + годинник + адреси (раз на секунду)

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // Тут можна додати інший код, який має виконуватись постійно,
    // наприклад, перевірка кнопок або мигання світлодіодом.
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 1599;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 49;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
  /* DMA2_Stream3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
