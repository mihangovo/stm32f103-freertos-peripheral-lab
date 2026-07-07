#include "led_task.h"
#include "main.h"
#include "storage_task.h"
#include "stdio.h"

extern osMutexId_t MetaDataMutexHandle;

static uint8_t g_led_red_state = 0;   // 0=灭, 1=亮

// 根据状态实际驱动GPIO
static void LED_Red_ApplyState(uint8_t state)
{
    HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, state ? GPIO_PIN_RESET : GPIO_PIN_SET);
    // 注意：如果你的红灯是低电平点亮，上面这行是对的；如果是高电平点亮，把RESET/SET换一下
}

void LED_Red_Toggle(void)
{
    g_led_red_state = !g_led_red_state;
    LED_Red_ApplyState(g_led_red_state);

    // 同步更新到Storage的meta数据，并请求保存
    MetaData_t *meta = Storage_GetMeta();
    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    meta->led_red_state = g_led_red_state;
    osMutexRelease(MetaDataMutexHandle);
    Storage_RequestSaveState();
}

uint8_t LED_Red_GetState(void)
{
    return g_led_red_state;
}

void LED_Task_Entry(void *argument)
{
    // 上电时，从Storage恢复红灯状态
    MetaData_t *meta = Storage_GetMeta();
    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    g_led_red_state = meta->led_red_state;
    osMutexRelease(MetaDataMutexHandle);

    // printf("LED task=%d\r\n", g_led_red_state);
    printf("LED Task Start\r\n");
    LED_Red_ApplyState(g_led_red_state);

    for(;;)
    {
        osDelay(100);   // 这个任务目前不需要持续工作，红灯只在按键触发时切换状态，主循环留空即可
    }
}