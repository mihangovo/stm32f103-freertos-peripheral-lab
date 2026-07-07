#ifndef __LED_TASK_H
#define __LED_TASK_H

#include "cmsis_os.h"

void LED_Task_Entry(void *argument);

// 供UI任务调用，切换红灯状态（会自动触发Flash保存）
void LED_Red_Toggle(void);
uint8_t LED_Red_GetState(void);

#endif