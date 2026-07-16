#ifndef __UI_TASK_H
#define __UI_TASK_H

#include "cmsis_os.h"

// ui_task.h
typedef enum {
    UI_MAIN_MENU = 0,
    UI_ATTITUDE_PAGE,
    UI_STORAGE_PAGE,
    UI_SETTING_PAGE,
    UI_LED_MENU,
    UI_LED_ONBOARD_PAGE,
    UI_UART_MONITOR_PAGE,
    UI_WS2812_PAGE,
    UI_PAGE_COUNT
} UI_State_t;

void UI_Manager_Task_Entry(void *argument);

#endif
