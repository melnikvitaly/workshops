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

// Драйвери I2C-пристроїв (порт з workshop-4-2, адаптований під STM32 HAL)
#include "ds1307.h"       // RTC годинник реального часу (0x68)
#include "ssd1306.h"      // OLED дисплей (0x3C)
#include "i2c_scanner.h"  // сканер шини I2C
#include "cat.h"          // мордочка кота на дисплеї
#include "eeprom.h"       // журнал у зовнішній EEPROM (0x50)
#include "adc.h"          // вимірювання освітлення через АЦП
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
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

// Об'єкти та стан для дисплея / годинника / сканера
Ssd1306  oled;
uint8_t  oled_ok = 0;
uint32_t last_scan_time = 0;   // коли востаннє сканували шину
uint32_t last_frame_time = 0;  // коли востаннє оновлювали кадр на екрані
char     devices[32] = "0x00"; // останній результат скану (у hex)
int16_t  ear_tilt = 0;         // чергуємо -> вуха ворушаться

// Розташування кота (нижче, щоб зверху помістилися годинник + адреси)
#define CAT_CX 64
#define CAT_CY 46
#define CAT_R  14
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// Функції роботи з EEPROM винесено в окремий драйвер (Inc/eeprom.h)
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
  current_address = EEPROM_GetNextAddress(&hi2c1);

  // Ініціалізація OLED-дисплея (адреса 0x3C, 128x64) на тій самій шині I2C1
  SSD1306_Setup(&oled, &hi2c1, 0x3C, 128, 64);
  oled_ok = (SSD1306_Init(&oled) == HAL_OK);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // Неблокуюча перевірка таймера
    if (HAL_GetTick() - last_log_time >= LOG_INTERVAL || last_log_time == 0) {

        // 1. Одне вимірювання освітлення у відсотках (0...100)
        uint8_t light_percent = 0;
        if (ADC_ReadPercent(&hadc1, &light_percent) == HAL_OK) {

            // 2. Записуємо в EEPROM, якщо є вільне місце
            if (current_address <= EEPROM_DATA_END) {
                EEPROM_WriteByte(&hi2c1, current_address, light_percent);

                // Зсуваємо вказівник і зберігаємо його
                current_address++;
                EEPROM_SaveNextAddress(&hi2c1, current_address);
            }
        }

        // Оновлюємо мітку часу
        last_log_time = HAL_GetTick();
    }

    // --- Періодичне сканування шини I2C (кожні 10 секунд) ---
    uint32_t now = HAL_GetTick();
    if (now - last_scan_time >= 10000 || last_scan_time == 0) {
        uint8_t addrs[8];
        int n = I2CScanner_Scan(&hi2c1, addrs, 8);
        last_scan_time = now;

        // Формуємо рядок "0x3C 0x68 ..." (стільки, скільки влізе)
        int pos = 0;
        devices[0] = '\0';
        for (int i = 0; i < n && i < 4; ++i) {
            pos += snprintf(devices + pos, sizeof(devices) - pos,
                            "%s0x%02X", (i ? " " : ""), addrs[i]);
        }
        if (n == 0) {
            snprintf(devices, sizeof(devices), "0x00");  // нічого не знайдено
        }
    }

    // --- Оновлення кадру раз на секунду: кіт + годинник + адреси ---
    if (now - last_frame_time >= 1000 || last_frame_time == 0) {
        last_frame_time = now;

        RtcTime t = {0};
        char clock[16] = "--:--:--";
        if (DS1307_ReadTime(&hi2c1, &t) == HAL_OK) {
            snprintf(clock, sizeof(clock), "%02d:%02d:%02d",
                     t.hours, t.minutes, t.seconds);
        }

        if (oled_ok) {
            // Кадр: кіт (без flush) + годинник зверху по центру, потім flush
            ear_tilt = (ear_tilt == 0) ? 5 : 0;  // проста анімація вух
            Cat_Draw(&oled, CAT_CX, CAT_CY, CAT_R, ear_tilt, /*flush=*/0);

            const uint8_t scale = 2;  // 10x14 пікселів на символ
            int16_t tx = (int16_t)((oled.width - SSD1306_TextWidth(clock, scale)) / 2);
            SSD1306_DrawText(&oled, tx, 0, clock, scale);  // годинник у верхньому рядку

            // Адреси знайдених I2C-пристроїв (дрібним шрифтом під годинником)
            int16_t dx = (int16_t)((oled.width - SSD1306_TextWidth(devices, 1)) / 2);
            SSD1306_DrawText(&oled, dx, 16, devices, 1);
            SSD1306_Flush(&oled);
        }
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
