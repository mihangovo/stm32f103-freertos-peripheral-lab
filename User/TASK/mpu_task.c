#include "mpu_task.h"
#include "main.h"
#include "mpu6050.h"
#include "inv_mpu.h"
#include "inv_mpu_dmp_motion_driver.h"
#include "stdio.h"
#include "task.h"
#include "watchdog_task.h"
extern osMutexId_t I2CMutexHandle;
extern osMutexId_t AttitudeMutexHandle;

float g_pitch = 0.0f;
float g_roll  = 0.0f;
float g_yaw   = 0.0f;

volatile uint32_t g_mpu_read_period = 200;

#define MPU_READ_PERIOD_MS   50   // DMP数据更新频率通常在50Hz左右，20ms采样一次比较合适

void MPU_Read_Task_Entry(void *argument)
{
    float pitch, roll, yaw;

    for(;;)
    {
        Watchdog_Checkin(WDG_TASK_MPU);

        osMutexAcquire(I2CMutexHandle, osWaitForever);
        int result = mpu_dmp_get_data(&pitch, &roll, &yaw);
        osMutexRelease(I2CMutexHandle);

        if(result == 0)   // 读取成功
        {
            osMutexAcquire(AttitudeMutexHandle, osWaitForever);
            g_pitch = pitch;
            g_roll  = roll;
            g_yaw   = yaw;
            osMutexRelease(AttitudeMutexHandle);
        }
// static uint32_t debug_counter = 0;
// if(++debug_counter >= 50)   // 20ms*50=1秒打印一次
// {
//     debug_counter = 0;
//     UBaseType_t mpu_free  = uxTaskGetStackHighWaterMark(NULL);  // NULL表示查询"调用者自己"的栈余量
//     printf("MPU task stack free: %lu words\r\n", (unsigned long)mpu_free);
// }
        osDelay(g_mpu_read_period);
    }
}