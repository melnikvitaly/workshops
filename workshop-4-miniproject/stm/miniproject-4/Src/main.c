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
#include "cat.h"           // мордочка кота на дисплеї
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
int16_t  ear_tilt = 0;         // чергуємо -> вуха ворушаться

// Рядки одного кадру (показуємо на екрані щокадру). Шрифт має лише
// цифри/A-F/x/':'/пробіл, тож стан I2C-шини показуємо у hex.
typedef struct {
    char devices[DEVICES_STR_LEN];  // "0x3C 0x68 ..." — результат сканування
    char clock[CLOCK_STR_LEN];      // "HH:MM:SS" — час з RTC
} FrameData;

FrameData frame = { "0x00", "--:--:--" };  // плейсхолдери до першого скану/читання
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
        EEPROM_WriteByte(&hi2c1, current_address, light_percent);
        current_address++;                                  // зсуваємо вказівник
        EEPROM_SaveNextAddress(&hi2c1, current_address);    // і зберігаємо його
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

// Намалювати один кадр: кіт + годинник зверху + адреси знайдених пристроїв.
static void render_frame(void) {
    // Нахил вух чергується щокадру -> вуха ворушаться (стан між викликами).
    ear_tilt = (ear_tilt == 0) ? EAR_TILT_MAX : 0;  // проста анімація вух
    Cat_Draw(&oled, CAT_CX, CAT_CY, CAT_R, ear_tilt, /*flush=*/0);

    Text_DrawTextCentered(&oled, CLOCK_Y, frame.clock, CLOCK_SCALE);        // годинник зверху
    Text_DrawTextCentered(&oled, DEVICES_Y, frame.devices, DEVICES_SCALE);  // адреси під ним
    SSD1306_Flush(&oled);
}

// Раз на FRAME_PERIOD_MS оновити час з RTC і перемалювати кадр.
static void poll_frame(void) {
    if (last_frame_time != 0 && HAL_GetTick() - last_frame_time < FRAME_PERIOD_MS) {
        return;
    }
    last_frame_time = HAL_GetTick();

    // frame.clock лишається без змін, якщо RTC не відповів (показуємо старий час).
    DS1307_ReadTimeString(&hi2c1, frame.clock, sizeof(frame.clock));

    if (oled.ready) {
        render_frame();
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
