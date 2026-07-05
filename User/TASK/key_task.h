#ifndef __KEY_TASK_H
#define __KEY_TASK_H

#include "cmsis_os.h"

// ---------- 按键事件定义 ----------
typedef enum {
    KEY_NONE = 0,
    KEY0_SHORT,
    KEY0_LONG,
    KEY1_SHORT,
    KEY1_LONG,
    KEY_UP_SHORT,
    KEY_UP_LONG,
} KeyEvent_t;

// 供 freertos.c 里的 Key_Scan_Task 调用的实际执行函数
void Key_Scan_Task_Entry(void *argument);

#endif
