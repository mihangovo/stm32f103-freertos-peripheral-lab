#include "watchdog_task.h"
#include "iwdg.h"
#include "stdio.h"

#define WDG_CHECK_PERIOD_MS     500
#define WDG_STALE_THRESHOLD_MS  1500

static volatile uint32_t g_task_alive_tick[WDG_TASK_COUNT];

static const char *g_task_name[WDG_TASK_COUNT] = {
    "Key", "UI", "MPU", "Heartbeat", "Storage", "Led"
};

void Watchdog_Checkin(WatchdogTaskId_t id)
{
    g_task_alive_tick[id] = osKernelGetTickCount();
}

void Watchdog_Task_Entry(void *argument)
{
    for(;;)
    {
        osDelay(WDG_CHECK_PERIOD_MS);

        uint32_t now = osKernelGetTickCount();
        uint8_t all_alive = 1;

        for(int i = 0; i < WDG_TASK_COUNT; i++)
        {
            if((now - g_task_alive_tick[i]) > WDG_STALE_THRESHOLD_MS)
            {
                printf("[Watchdog] task %s stale, skip feed\r\n", g_task_name[i]);
                all_alive = 0;
            }
        }

        if(all_alive)
        {
            HAL_IWDG_Refresh(&hiwdg);
        }
    }
}
