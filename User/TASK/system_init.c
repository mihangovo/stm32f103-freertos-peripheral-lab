#include "system_init.h"
#include "main.h"
#include "iwdg.h"
#include "myiic.h"
#include "oled.h"
#include "norflash.h"
#include "mpu6050.h"
#include "inv_mpu.h"
#include "storage_task.h"
#include "usart.h"
#include "watchdog_task.h"
#include "can_task.h"
#include <stdio.h>

static void Init_ShowStage(const char *stage, const char *detail)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t *)"System Init", 16, 1);
    OLED_ShowString(0, 24, (uint8_t *)stage, 12, 1);
    OLED_ShowString(0, 42, (uint8_t *)detail, 12, 1);
    OLED_Refresh();
    printf("[Init] %s: %s\r\n", stage, detail);
}

void System_Init_WaitReady(void)
{
    (void)osEventFlagsWait(SystemReadyFlagsHandle, SYSTEM_READY_FLAG,
                            osFlagsWaitAny | osFlagsNoClear, osWaitForever);
}

void System_Init_ReleaseTasks(void)
{
    (void)osEventFlagsSet(SystemReadyFlagsHandle, SYSTEM_READY_FLAG);
}

void Init_Task_Entry(void *argument)
{
    (void)argument;

    UART_Rx_Start();
    iic_init();
    OLED_Init();

    Init_ShowStage("Flash", "Initializing");
    norflash_init();
    uint16_t id = norflash_read_id();
    printf("NORFLASH ID: 0x%04X\r\n", id);

    Init_ShowStage("MPU6050", "Initializing");
    while (MPU_Init() != 0)
    {
        Init_ShowStage("MPU6050", "Retrying");
        osDelay(200);
    }

    Init_ShowStage("DMP", "Initializing");
    while (mpu_dmp_init() != 0)
    {
        Init_ShowStage("DMP", "Retrying");
        osDelay(200);
    }

    Init_ShowStage("Storage", "Loading state");
    Meta_Load();
    Init_ShowStage("CAN", "Starting loopback");
    Can_InitStart();

    Init_ShowStage("Ready", "Starting tasks");
    Watchdog_ResetCheckins();
    MX_IWDG_Init();
    System_Init_ReleaseTasks();
    osThreadExit();
}
