#include "can_task.h"
#include "can.h"
#include "system_init.h"
#include "watchdog_task.h"
#include <string.h>

#define CAN_LOOPBACK_ID         0x321U
#define CAN_SEND_PERIOD_MS      500U

static volatile CanStats_t g_can_stats;

void Can_InitStart(void)
{
    CAN_FilterTypeDef filter = {0};
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = CAN_FILTER_ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK ||
        HAL_CAN_Start(&hcan) != HAL_OK ||
        HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        ++g_can_stats.error_count;
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_handle)
{
    CanFrame_t frame;
    if (hcan_handle->Instance != CAN1 ||
        HAL_CAN_GetRxMessage(hcan_handle, CAN_RX_FIFO0, &frame.header, frame.data) != HAL_OK)
    {
        ++g_can_stats.error_count;
        return;
    }

    if (osMessageQueuePut(CanRxQueueHandle, &frame, 0U, 0U) != osOK)
    {
        ++g_can_stats.drop_count;
    }
}

void Can_GetStats(CanStats_t *stats)
{
    *stats = g_can_stats;
}

void Can_Task_Entry(void *argument)
{
    uint32_t sequence = 0U;
    (void)argument;
    System_Init_WaitReady();

    for (;;)
    {
        CAN_TxHeaderTypeDef header = {0};
        uint8_t data[8] = {0};
        uint32_t mailbox;

        header.StdId = CAN_LOOPBACK_ID;
        header.IDE = CAN_ID_STD;
        header.RTR = CAN_RTR_DATA;
        header.DLC = 4U;
        memcpy(data, &sequence, sizeof(sequence));

        if (HAL_CAN_AddTxMessage(&hcan, &header, data, &mailbox) == HAL_OK)
        {
            ++g_can_stats.tx_count;
            ++sequence;
        }
        else
        {
            ++g_can_stats.error_count;
        }

        CanFrame_t frame;
        while (osMessageQueueGet(CanRxQueueHandle, &frame, NULL, 0U) == osOK)
        {
            ++g_can_stats.rx_count;
            g_can_stats.last_id = frame.header.StdId;
        }

        Watchdog_Checkin(WDG_TASK_CAN);
        osDelay(CAN_SEND_PERIOD_MS);
    }
}
