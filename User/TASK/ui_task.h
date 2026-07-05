#ifndef __UI_TASK_H
#define __UI_TASK_H

#include "cmsis_os.h"

typedef enum {
    UI_MAIN_MENU = 0,
    UI_ATTITUDE_PAGE,
    UI_STORAGE_PAGE,
    UI_SETTING_PAGE,
    UI_PAGE_COUNT   // 菜单项总数，方便循环边界判断
} UI_State_t;

void UI_Manager_Task_Entry(void *argument);

#endif