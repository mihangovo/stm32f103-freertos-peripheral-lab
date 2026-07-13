/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "dma.h"
#include "iwdg.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "myiic.h"
#include "oled.h"
#include "mpu6050.h"
#include "inv_mpu.h"
#include "inv_mpu_dmp_motion_driver.h"
#include <stdio.h>
#include "norflash.h"
#include <string.h>
#include "storage_task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
//   float g_pitch = 0.0f;
// float g_roll  = 0.0f;
// float g_yaw   = 0.0f;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  MX_SPI2_Init();
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
  {
      printf("[Watchdog] last reset was caused by IWDG (task hang recovered)\r\n");
  }
  __HAL_RCC_CLEAR_RESET_FLAGS();

  UART_Rx_Start();
 HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
 __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 19999*12.5/100); // 将占空比调整到 50%

  iic_init();     /* 初始化IIC总线 */  
  OLED_Init();   /* 初始化OLED */ 
  OLED_ShowString(0, 0,(uint8_t *)"OLED Init finished", 16, 1);
  OLED_Refresh();
  printf("usart initialized\r\n");

  //flash测试
  norflash_init();

  uint16_t id = norflash_read_id();
  printf("NORFLASH ID: 0x%04X\r\n", id);

  if (id == 0 || id == 0xFFFF) {
    printf("FLASH Check Failed! Please check wiring.\r\n");
  } else if (id == W25Q128) {
    printf("FLASH Check OK, chip is W25Q128.\r\n");
  } else {
    printf("FLASH detected, but ID does not match W25Q128 (got 0x%04X)\r\n",
           id);
  }

  // const uint8_t test_text[] = "STM32 SPI TEST";
  // uint8_t write_buf[32];
  // uint8_t read_buf[32] = {0};
  // uint32_t flash_size = 16 * 1024 * 1024; // W25Q128 = 16MB
  // uint32_t test_addr = flash_size - 100;

  // sprintf((char *)write_buf, "%s", test_text);
  // norflash_write(write_buf, test_addr, sizeof(test_text));

  // norflash_read(read_buf, test_addr, sizeof(test_text));

  // printf("Write: %s\r\n", write_buf);
  // printf("Read : %s\r\n", read_buf);

  // if (memcmp(write_buf, read_buf, sizeof(test_text)) == 0) {
  //   printf("Flash R/W test: PASS\r\n");
  // } else {
  //   printf("Flash R/W test: FAIL\r\n");
  // }


  // 初始化MPU6050
  if (MPU_Init() != 0) {
    printf("mpu_init err");
    OLED_ShowString(0, 16,(uint8_t *)"MPU6050 Init failed", 16, 1);
    OLED_Refresh();
    while (1)
      ;
  }

  // 初始化DMP
  while (mpu_dmp_init()) {
    HAL_Delay(200);
    printf("dmp_init err");
    OLED_ShowString(0, 32,(uint8_t *)"dmp_init failed", 16, 1);
    OLED_Refresh();
    // 重试
  }

  OLED_Clear();
  OLED_Refresh();

  Meta_Load();

  
  uint16_t i = 0;
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // if (mpu_dmp_get_data(&g_pitch, &g_roll, &g_yaw) == 0) {
    //   // 成功读取数据
    //   printf("Pitch:%.2f Roll:%.2f Yaw:%.2f\r\n", g_pitch, g_roll, g_yaw);
    //   OLED_ShowFloatNum(0, 0, g_pitch, 2, 2, 16,1);
    //   OLED_ShowFloatNum(0, 16, g_roll , 2, 2, 16,1);
    //   OLED_ShowFloatNum(0, 32, g_yaw , 2, 2, 16,1);
    //   OLED_Refresh();
    // }
    HAL_Delay(10); // 10Hz读取频率
    i++;

    if (i == 20) {
      HAL_GPIO_TogglePin(RED_GPIO_Port, RED_Pin);
      i = 0;
    }
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM8 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM8) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
