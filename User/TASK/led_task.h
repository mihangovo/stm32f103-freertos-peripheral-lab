#ifndef __LED_TASK_H
#define __LED_TASK_H

#include "cmsis_os.h"


void LED_Task_Entry(void *argument);
uint8_t LED_Red_GetState(void);   // 这个查询接口可以保留，供UI显示用
void LED_Red_Toggle(void); // 改成"请求"而不是直接执行

#endif