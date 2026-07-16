#ifndef __CAN_TASK_H
#define __CAN_TASK_H

#include "cmsis_os.h"
#include "stm32f1xx_hal.h"

typedef struct {
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
} CanFrame_t;

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t error_count;
    uint32_t drop_count;
    uint32_t last_id;
} CanStats_t;

extern osMessageQueueId_t CanRxQueueHandle;

void Can_Task_Entry(void *argument);
void Can_InitStart(void);
void Can_GetStats(CanStats_t *stats);

#endif
