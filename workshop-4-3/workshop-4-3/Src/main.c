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
// Тут можна додати власні бібліотеки, якщо знадобляться
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EEPROM_ADDR (0x50 << 1)  // Зсунута 7-бітна адреса для HAL

// 1 година в мілісекундах (60 хвилин * 60 секунд * 1000)
// ⚠️ ПОРАДА: Для перевірки роботи на уроці змініть це значення на 5000 (5 секунд)
#define LOG_INTERVAL 3600000
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN PV */
// Змінні для керування логуванням
uint32_t last_log_time = 0;
uint16_t current_address = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */
uint8_t EEPROM_ReadByte(uint16_t mem_address);
void EEPROM_WriteByte(uint16_t mem_address, uint8_t data);
uint16_t Get_Next_Address(void);
void Save_Next_Address(uint16_t ptr);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// Читання 1 байта
uint8_t EEPROM_ReadByte(uint16_t mem_address) {
    uint8_t data = 0;
    HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT, &data, 1, 100);
    return data;
}

// Запис 1 байта
void EEPROM_WriteByte(uint16_t mem_address, uint8_t data) {
    HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT, &data, 1, 100);
    HAL_Delay(5); // Затримка 5 мс для фізичного запису в EEPROM (обов'язково!)
}

// Читання вказівника (адреси наступного запису) з перших двох байтів пам'яті
uint16_t Get_Next_Address(void) {
    uint8_t buf[2];
    HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDR, 0x0000, I2C_MEMADD_SIZE_16BIT, buf, 2, 100);

    // Об'єднання за стандартом Big-Endian
    uint16_t ptr = ((uint16_t)buf[0] << 8) | buf[1];

    // Якщо пам'ять чиста (0xFFFF) або адреса некоректна, починаємо з 0x0002
    if (ptr < 0x0002 || ptr > 0x0FFF) {
        return 0x0002;
    }
    return ptr;
}

// Збереження нового вказівника
void Save_Next_Address(uint16_t ptr) {
    uint8_t buf[2];
    buf[0] = (ptr >> 8) & 0xFF; // MSB (старший байт)
    buf[1] = ptr & 0xFF;        // LSB (молодший байт)

    HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDR, 0x0000, I2C_MEMADD_SIZE_16BIT, buf, 2, 100);
    HAL_Delay(5);
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
  MX_ADC1_Init();
  MX_I2C1_Init();

  /* USER CODE BEGIN 2 */
  // Зчитуємо адресу з пам'яті під час старту пристрою
  current_address = Get_Next_Address();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // Неблокуюча перевірка таймера
    if (HAL_GetTick() - last_log_time >= LOG_INTERVAL || last_log_time == 0) {

        // 1. Запуск вимірювання АЦП
        HAL_ADC_Start(&hadc1);

        // Чекаємо завершення конвертації (таймаут 10 мс)
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {

            // Отримуємо сире 12-бітне значення (0...4095)
            uint32_t adc_value = HAL_ADC_GetValue(&hadc1);

            // 2. Математика: переводимо у відсотки (0...100)
            uint8_t light_percent = (adc_value * 100) / 4095;

            // 3. Записуємо в EEPROM, якщо є вільне місце
            if (current_address <= 0x0FFF) {
                EEPROM_WriteByte(current_address, light_percent);

                // Зсуваємо вказівник і зберігаємо його
                current_address++;
                Save_Next_Address(current_address);
            }
        }
        HAL_ADC_Stop(&hadc1); // Зупиняємо АЦП до наступного разу

        // Оновлюємо мітку часу
        last_log_time = HAL_GetTick();
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // Тут можна додати інший код, який має виконуватись постійно,
    // наприклад, перевірка кнопок або мигання світлодіодом.
  }
  /* USER CODE END 3 */
}

// ... (Нижче залишаються стандартні функції SystemClock_Config, MX_ADC1_Init, MX_I2C1_Init тощо, які згенерував CubeMX. Я їх не дублюю, щоб не захаращувати відповідь, але вони залишаються без змін!)

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
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
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
