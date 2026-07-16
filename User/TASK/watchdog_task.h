#ifndef __WATCHDOG_TASK_H
#define __WATCHDOG_TASK_H

#include "cmsis_os.h"

typedef enum {
    WDG_TASK_KEY = 0,
    WDG_TASK_UI,
    WDG_TASK_MPU,
    WDG_TASK_HEARTBEAT,
    WDG_TASK_STORAGE,
    WDG_TASK_LED,
    WDG_TASK_CAN,
    WDG_TASK_COUNT
} WatchdogTaskId_t;

// 各被监控任务在自己主循环里调用，登记"我还活着"
void Watchdog_Checkin(WatchdogTaskId_t id);
void Watchdog_ResetCheckins(void);

// 供 freertos.c 里的 Watchdog_Task 调用的实际执行函数
void Watchdog_Task_Entry(void *argument);

#endif
