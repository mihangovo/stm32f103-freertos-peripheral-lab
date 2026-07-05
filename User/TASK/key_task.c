#include "key_task.h"
#include "main.h"

#define LONG_PRESS_TICKS   pdMS_TO_TICKS(800)
#define DEBOUNCE_MS        20
#define SCAN_PERIOD_MS     10

extern osMessageQueueId_t KeyQueueHandle;   // freertos.c 里已经创建好的队列句柄

static uint8_t Read_Key_Pin(uint8_t key_index)
{
    switch(key_index)
    {
        case 0: return HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin);
        case 1: return HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin);
        case 2: return !HAL_GPIO_ReadPin(KEY_UP_GPIO_Port, KEY_UP_Pin);
        default: return 1;
    }
}

void Key_Scan_Task_Entry(void *argument)
{
    uint8_t key_last_state[3] = {1, 1, 1};
    uint32_t press_start_tick[3] = {0};

    for(;;)
    {
        for(int i = 0; i < 3; i++)
        {
            uint8_t cur = Read_Key_Pin(i);

            // ---- 检测到按下（下降沿）----
            if(cur == 0 && key_last_state[i] == 1)
            {
                osDelay(DEBOUNCE_MS);
                if(Read_Key_Pin(i) == 0)
                {
                    press_start_tick[i] = osKernelGetTickCount();
                    key_last_state[i] = 0;
                }
            }
            // ---- 检测到松开（上升沿）----
            else if(cur == 1 && key_last_state[i] == 0)
            {
                osDelay(DEBOUNCE_MS);
                if(Read_Key_Pin(i) == 1)
                {
                    uint32_t duration = osKernelGetTickCount() - press_start_tick[i];
                    uint16_t evt;

                    if(duration >= LONG_PRESS_TICKS)
                        evt = (uint16_t)(KEY0_LONG + i * 2);
                    else
                        evt = (uint16_t)(KEY0_SHORT + i * 2);

                    osMessageQueuePut(KeyQueueHandle, &evt, 0, 0);

                    key_last_state[i] = 1;
                }
            }
        }

        osDelay(SCAN_PERIOD_MS);
    }
}