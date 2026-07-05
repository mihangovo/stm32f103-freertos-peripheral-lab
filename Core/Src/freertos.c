/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "key_task.h"
#include "oled.h"
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
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for KeyScanTask */
osThreadId_t KeyScanTaskHandle;
const osThreadAttr_t KeyScanTask_attributes = {
  .name = "KeyScanTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for UIManagerTask */
osThreadId_t UIManagerTaskHandle;
const osThreadAttr_t UIManagerTask_attributes = {
  .name = "UIManagerTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for MPUReadTask */
osThreadId_t MPUReadTaskHandle;
const osThreadAttr_t MPUReadTask_attributes = {
  .name = "MPUReadTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for KeyQueue */
osMessageQueueId_t KeyQueueHandle;
const osMessageQueueAttr_t KeyQueue_attributes = {
  .name = "KeyQueue"
};
/* Definitions for I2CMutex */
osMutexId_t I2CMutexHandle;
const osMutexAttr_t I2CMutex_attributes = {
  .name = "I2CMutex"
};
/* Definitions for AttitudeMutex */
osMutexId_t AttitudeMutexHandle;
const osMutexAttr_t AttitudeMutex_attributes = {
  .name = "AttitudeMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void Key_Scan_Task(void *argument);
void UI_Manager_Task(void *argument);
void MPU_Read_Task(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of I2CMutex */
  I2CMutexHandle = osMutexNew(&I2CMutex_attributes);

  /* creation of AttitudeMutex */
  AttitudeMutexHandle = osMutexNew(&AttitudeMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of KeyQueue */
  KeyQueueHandle = osMessageQueueNew (5, sizeof(uint16_t), &KeyQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of KeyScanTask */
  KeyScanTaskHandle = osThreadNew(Key_Scan_Task, NULL, &KeyScanTask_attributes);

  /* creation of UIManagerTask */
  UIManagerTaskHandle = osThreadNew(UI_Manager_Task, NULL, &UIManagerTask_attributes);

  /* creation of MPUReadTask */
  MPUReadTaskHandle = osThreadNew(MPU_Read_Task, NULL, &MPUReadTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_Key_Scan_Task */
/**
  * @brief  Function implementing the KeyScanTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_Key_Scan_Task */
void Key_Scan_Task(void *argument)
{
  /* USER CODE BEGIN Key_Scan_Task */
  Key_Scan_Task_Entry(argument);
  /* Infinite loop */
  // for(;;)
  // {
  //   osDelay(1);
  // }
  /* USER CODE END Key_Scan_Task */
}

/* USER CODE BEGIN Header_UI_Manager_Task */
/**
* @brief Function implementing the UIManagerTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_UI_Manager_Task */
void UI_Manager_Task(void *argument)
{
  /* USER CODE BEGIN UI_Manager_Task */
  uint16_t evt;
  /* Infinite loop */
  for(;;)
  {
    if(osMessageQueueGet(KeyQueueHandle, &evt, NULL, osWaitForever) == osOK)
    {
        OLED_Clear();
        OLED_ShowNum(0, 0, evt, 1, 16, 1);  // 在屏幕左上角显示事件编号�?0~6�?
        OLED_Refresh();
    }
    osDelay(10);
  }
  /* USER CODE END UI_Manager_Task */
}

/* USER CODE BEGIN Header_MPU_Read_Task */
/**
* @brief Function implementing the MPUReadTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_MPU_Read_Task */
void MPU_Read_Task(void *argument)
{
  /* USER CODE BEGIN MPU_Read_Task */
  /* Infinite loop */
  for(;;)
  {
    HAL_GPIO_TogglePin(RED_GPIO_Port, RED_Pin);
    osDelay(500);
   HAL_GPIO_TogglePin(RED_GPIO_Port, RED_Pin);
    osDelay(500);
  }
  /* USER CODE END MPU_Read_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

