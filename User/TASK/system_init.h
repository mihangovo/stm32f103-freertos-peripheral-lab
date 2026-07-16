#ifndef __SYSTEM_INIT_H
#define __SYSTEM_INIT_H

#include "cmsis_os.h"

#define SYSTEM_READY_FLAG  (1U << 0)

extern osEventFlagsId_t SystemReadyFlagsHandle;

void System_Init_WaitReady(void);
void System_Init_ReleaseTasks(void);
void Init_Task_Entry(void *argument);

#endif
