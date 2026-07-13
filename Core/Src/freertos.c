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
#include "ui_task.h"
#include "mpu_task.h"
#include "usart.h"
#include "stdio.h"
#include "led_task.h"
#include "storage_task.h"
#include "watchdog_task.h"
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
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for MPUReadTask */
osThreadId_t MPUReadTaskHandle;
const osThreadAttr_t MPUReadTask_attributes = {
  .name = "MPUReadTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for HeartbeatTask */
osThreadId_t HeartbeatTaskHandle;
const osThreadAttr_t HeartbeatTask_attributes = {
  .name = "HeartbeatTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for StorageTask */
osThreadId_t StorageTaskHandle;
const osThreadAttr_t StorageTask_attributes = {
  .name = "StorageTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for LedTask */
osThreadId_t LedTaskHandle;
const osThreadAttr_t LedTask_attributes = {
  .name = "LedTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for WatchdogTask */
osThreadId_t WatchdogTaskHandle;
const osThreadAttr_t WatchdogTask_attributes = {
  .name = "WatchdogTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for KeyQueue */
osMessageQueueId_t KeyQueueHandle;
const osMessageQueueAttr_t KeyQueue_attributes = {
  .name = "KeyQueue"
};
/* Definitions for StorageCmdQueue */
osMessageQueueId_t StorageCmdQueueHandle;
const osMessageQueueAttr_t StorageCmdQueue_attributes = {
  .name = "StorageCmdQueue"
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
/* Definitions for SPIMutex */
osMutexId_t SPIMutexHandle;
const osMutexAttr_t SPIMutex_attributes = {
  .name = "SPIMutex"
};
/* Definitions for MetaDataMutex */
osMutexId_t MetaDataMutexHandle;
const osMutexAttr_t MetaDataMutex_attributes = {
  .name = "MetaDataMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void Key_Scan_Task(void *argument);
void UI_Manager_Task(void *argument);
void MPU_Read_Task(void *argument);
void Hearbeat_Task(void *argument);
void Storage_Task(void *argument);
void Led_Task(void *argument);
void Watchdog_Task(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
       printf("Stack Overflow: %s\r\n", pcTaskName);
    while(1);
}
/* USER CODE END 4 */

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

  /* creation of SPIMutex */
  SPIMutexHandle = osMutexNew(&SPIMutex_attributes);

  /* creation of MetaDataMutex */
  MetaDataMutexHandle = osMutexNew(&MetaDataMutex_attributes);

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

  /* creation of StorageCmdQueue */
  StorageCmdQueueHandle = osMessageQueueNew (10, sizeof(StorageCmd_t), &StorageCmdQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  UartLineQueueHandle = osMessageQueueNew(16, sizeof(UartLine_t), NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of KeyScanTask */
  KeyScanTaskHandle = osThreadNew(Key_Scan_Task, NULL, &KeyScanTask_attributes);

  /* creation of UIManagerTask */
  UIManagerTaskHandle = osThreadNew(UI_Manager_Task, NULL, &UIManagerTask_attributes);

  /* creation of MPUReadTask */
  MPUReadTaskHandle = osThreadNew(MPU_Read_Task, NULL, &MPUReadTask_attributes);

  /* creation of HeartbeatTask */
  HeartbeatTaskHandle = osThreadNew(Hearbeat_Task, NULL, &HeartbeatTask_attributes);

  /* creation of StorageTask */
  StorageTaskHandle = osThreadNew(Storage_Task, NULL, &StorageTask_attributes);

  /* creation of LedTask */
  LedTaskHandle = osThreadNew(Led_Task, NULL, &LedTask_attributes);

  /* creation of WatchdogTask */
  WatchdogTaskHandle = osThreadNew(Watchdog_Task, NULL, &WatchdogTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  printf("[RTOS] free heap after all creation = %u bytes\r\n", (unsigned)xPortGetFreeHeapSize());
  printf("[RTOS] handles: Key=%p UI=%p MPU=%p Heartbeat=%p Storage=%p Led=%p\r\n",
         (void*)KeyScanTaskHandle, (void*)UIManagerTaskHandle, (void*)MPUReadTaskHandle,
         (void*)HeartbeatTaskHandle, (void*)StorageTaskHandle, (void*)LedTaskHandle);
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
  UI_Manager_Task_Entry(argument);
  // for(;;)
  // {
  //   osDelay(10);
  // }
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
MPU_Read_Task_Entry(argument);
  /* Infinite loop */
//   for(;;)
//   {
// osDelay(10);

//   }
  /* USER CODE END MPU_Read_Task */
}

/* USER CODE BEGIN Header_Hearbeat_Task */
/**
* @brief Function implementing the HeartbeatTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Hearbeat_Task */
void Hearbeat_Task(void *argument)
{
  /* USER CODE BEGIN Hearbeat_Task */
  /* Infinite loop */
  uint8_t i = 0;
  uint8_t hwm_counter = 0;
  for(;;)
  {
    Watchdog_Checkin(WDG_TASK_HEARTBEAT);
    // oled_Refresh();
    // osDelay(100);
    // i++;
    // if(i >= 20)   // 20ms*20=400ms闪烁
    // {
    //   HAL_GPIO_TogglePin(RED_GPIO_Port, RED_Pin);
    //   i = 0;
    // }
    HAL_GPIO_TogglePin(GREEN_GPIO_Port, GREEN_Pin);
    osDelay(500);
    HAL_GPIO_TogglePin(GREEN_GPIO_Port, GREEN_Pin);
    osDelay(500);

    // 诊断：每10秒打印各任务栈的历史最小剩余量(高水位线)，单位字节
    // 数值会随着各任务跑到过的最坏情况(OLED刷新/DMP解算/UART突发等)逐渐变小并趋于稳定
    hwm_counter++;
    if(hwm_counter >= 10)
    {
        hwm_counter = 0;
        printf("[StackHWM] Key=%lu UI=%lu MPU=%lu Heartbeat=%lu Storage=%lu Led=%lu (bytes free, min ever)\r\n",
               (unsigned long)(uxTaskGetStackHighWaterMark(KeyScanTaskHandle) * 4),
               (unsigned long)(uxTaskGetStackHighWaterMark(UIManagerTaskHandle) * 4),
               (unsigned long)(uxTaskGetStackHighWaterMark(MPUReadTaskHandle) * 4),
               (unsigned long)(uxTaskGetStackHighWaterMark(HeartbeatTaskHandle) * 4),
               (unsigned long)(uxTaskGetStackHighWaterMark(StorageTaskHandle) * 4),
               (unsigned long)(uxTaskGetStackHighWaterMark(LedTaskHandle) * 4));
    }
  }
  /* USER CODE END Hearbeat_Task */
}

/* USER CODE BEGIN Header_Storage_Task */
/**
* @brief Function implementing the StorageTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Storage_Task */
void Storage_Task(void *argument)
{
  /* USER CODE BEGIN Storage_Task */
  /* Infinite loop */

  Storage_Task_Entry(argument);
  // for(;;)
  // {
  //   osDelay(1);
  // }
  /* USER CODE END Storage_Task */
}

/* USER CODE BEGIN Header_Led_Task */
/**
* @brief Function implementing the LedTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Led_Task */
void Led_Task(void *argument)
{
  /* USER CODE BEGIN Led_Task */
  LED_Task_Entry(argument);
  /* Infinite loop */
  // for(;;)
  // {
  //   osDelay(1);
  // }
  /* USER CODE END Led_Task */
}

/* USER CODE BEGIN Header_Watchdog_Task */
/**
* @brief Function implementing the WatchdogTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Watchdog_Task */
void Watchdog_Task(void *argument)
{
  /* USER CODE BEGIN Watchdog_Task */
  Watchdog_Task_Entry(argument);
  /* USER CODE END Watchdog_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

