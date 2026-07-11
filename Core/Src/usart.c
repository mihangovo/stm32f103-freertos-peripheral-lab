/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
#include "storage_task.h"
#include "stdio.h"
#include <string.h>
/* USER CODE END 0 */

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART1 DMA Init */
    /* USART1_RX Init */
    hdma_usart1_rx.Instance = DMA1_Channel5;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart1_rx);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);

    /* USART1 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

// 放在main.c或者你的串口处理文件里
#define UART_RX_BUFFER_SIZE   256
uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
osMessageQueueId_t UartLineQueueHandle;

// DMA是循环模式，硬件会一直不停地收，不需要也不能在回调里重新调用
// HAL_UARTEx_ReceiveToIdle_DMA "重新武装"——那样做会让HAL内部记录的位置和硬件实际
// 写入位置对不上，导致Size越滚越大、把早就处理过的旧数据当成新数据重复处理。
// 正确做法是自己记住"上次处理到哪个位置"(uart_rd_pos)，每次只处理从上次位置到
// 这次Size之间真正新增的字节；如果Size比上次小，说明缓冲区绕回了开头，要分两段处理。
static uint16_t uart_rd_pos = 0;

// 跨事件(甚至跨绕回边界)拼接一行内容，直到遇到'\n'才算一行结束
static uint8_t  uart_line_acc[UART_LINE_MAXLEN];
static uint16_t uart_line_acc_len = 0;

// 在初始化阶段（比如main()里，MX_USARTx_Init()之后）调用一次
// 注意：UartLineQueueHandle 不在这里创建——这个函数在main()里调度器启动前很早就被调用，
// 在FreeRTOS调度器真正启动(osKernelStart)之前创建内核对象曾经导致过tick中断被永久屏蔽
// (HAL_Delay全部卡死)，所以队列创建挪到了 freertos.c 的 MX_FREERTOS_Init() 里，
// 跟其他队列(如StorageCmdQueueHandle)一样，在已验证安全的位置创建。
void UART_Rx_Start(void)
{
    uart_rd_pos = 0;
    uart_line_acc_len = 0;
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart_rx_buffer, UART_RX_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);   // 禁用DMA半传输中断，避免不必要的触发
}

// 保存并推送一条已经拆分干净的单行数据(不含\r\n)
static void Uart_Emit_Line(const uint8_t *data, uint16_t len)
{
    if(len == 0)
    {
        return;
    }

    Storage_RequestSaveHistory((char*)data, len);

    UartLine_t mon_line;
    uint16_t mon_len = (len > UART_LINE_MAXLEN) ? UART_LINE_MAXLEN : len;
    memcpy(mon_line.text, data, mon_len);
    mon_line.len = mon_len;
    osMessageQueuePut(UartLineQueueHandle, &mon_line, 0, 0);
}

// 把新增的这一段原始字节逐个喂进拼接缓冲区，遇到'\n'就把攒好的一行发出去
static void Uart_Feed_New_Bytes(const uint8_t *data, uint16_t len)
{
    for(uint16_t i = 0; i < len; i++)
    {
        printf("%c", data[i]);

        uint8_t c = data[i];

        if(c == '\n')
        {
            uint16_t line_len = uart_line_acc_len;
            if((line_len > 0) && (uart_line_acc[line_len - 1] == '\r'))
            {
                line_len--;
            }
            Uart_Emit_Line(uart_line_acc, line_len);
            uart_line_acc_len = 0;
        }
        else if(uart_line_acc_len < UART_LINE_MAXLEN)
        {
            uart_line_acc[uart_line_acc_len++] = c;
        }
        // 超过UART_LINE_MAXLEN的部分直接丢弃，单行内容本来就只保留前面这些字节
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if(huart->Instance == USART1)
    {
        if(Size == uart_rd_pos)
        {
            return;   // 没有新数据(理论上不会触发，防御性判断)
        }

        printf("\r\nUART Rx Size=%d\r\nUART Data=", Size);

        if(Size > uart_rd_pos)
        {
            // 没有绕回，新数据是一段连续区间
            Uart_Feed_New_Bytes(&uart_rx_buffer[uart_rd_pos], Size - uart_rd_pos);
        }
        else
        {
            // 缓冲区绕回了开头，新数据分两段：[uart_rd_pos, 末尾) 和 [0, Size)
            Uart_Feed_New_Bytes(&uart_rx_buffer[uart_rd_pos], UART_RX_BUFFER_SIZE - uart_rd_pos);
            Uart_Feed_New_Bytes(&uart_rx_buffer[0], Size);
        }

        printf("\r\n");

        uart_rd_pos = Size;
    }
}
/* USER CODE END 1 */
