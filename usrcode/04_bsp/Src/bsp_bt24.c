/**
 ******************************************************************************
 * @file    bsp_bt24.c
 * @brief   DX-BT24 BLE串口透传模块板级驱动实现 — USART2 DMA循环接收 + IDLE判帧
 *
 *  设计要点：
 *    1. 完全复用 bsp_uart.c 的 UART1/UART3 成熟模式（DMA循环 + IDLE判帧 +
 *       软件环形缓冲），仅更换为 USART2 外设与独立缓冲区，保持 BSP 层
 *       RTOS 无关、可单独移植。
 *    2. 发送使用 HAL_UART_Transmit 阻塞发送（任务上下文调用）；
 *       9600bps 下发送超时放宽到 500ms，避免长帧（~150B JSON）被截断。
 *    3. 错误恢复（ORE/DMA Error）统一挂在 HAL_UART_ErrorCallback
 *       （bsp_uart.c 中定义，本文件仅提供 USART2 分支的恢复函数，避免
 *       重复定义 HAL 弱函数）。
 ******************************************************************************
 */
#include "module_cfg.h"

#if BSP_BT24_ENABLE

#include "bsp_bt24.h"
#include "ring_buffer.h"
#include "main.h"
#include "usart.h"
#include "dma.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ========== 外部声明（不修改MX文件，直接在此声明） ========== */
extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_rx;

/* ========== 静态资源 ========== */
static uint8_t        s_bt24_dma_rx_buf[BSP_BT24_RX_DMA_BUF_SIZE];   /* DMA环形缓冲 */
static uint8_t        s_bt24_ring_buf[BSP_BT24_RX_RING_BUF_SIZE];    /* 软件环形缓冲 */
static ring_buffer_t  s_bt24_rx_ring;
static volatile uint32_t s_bt24_dma_last_pos;                        /* DMA缓冲已消费位置 */

/* ========== 内部函数 ========== */
static void BSP_BT24_StartRxDma(void);

/**
 * @brief  启动DMA循环接收并使能IDLE中断
 */
static void BSP_BT24_StartRxDma(void)
{
    HAL_UART_Receive_DMA(&huart2, s_bt24_dma_rx_buf, BSP_BT24_RX_DMA_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
}

void BSP_BT24_Init(void)
{
    RB_Init(&s_bt24_rx_ring, s_bt24_ring_buf, sizeof(s_bt24_ring_buf));
    s_bt24_dma_last_pos = 0U;
    BSP_BT24_StartRxDma();
}

/**
 * @brief  IDLE空闲中断处理（判帧）
 *
 *  原理（与 bsp_uart 一致）：
 *    - DMA计数器(NDTR)递减计数，已收字节数 = 缓冲大小 - 当前计数值
 *    - 与上次消费位置做差，得到本次新数据长度（支持环形绕回）
 *    - 将新数据从 DMA 环形缓冲搬到软件环形缓冲，交给上层任务
 */
void BSP_BT24_RxIdleHandler(void)
{
    uint32_t pos   = BSP_BT24_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
    uint32_t start = s_bt24_dma_last_pos;
    uint32_t len;
    uint32_t first;

    if (pos >= start) {
        len = pos - start;
    } else {
        len = BSP_BT24_RX_DMA_BUF_SIZE - start + pos;   /* 环形绕回 */
    }
    s_bt24_dma_last_pos = pos;

    if (len == 0U) {
        return;
    }

    /* 从DMA环形缓冲拷贝到软件环形缓冲（可能跨缓冲末尾，分两段） */
    first = BSP_BT24_RX_DMA_BUF_SIZE - start;
    if (len <= first) {
        RB_Write(&s_bt24_rx_ring, &s_bt24_dma_rx_buf[start], len);
    } else {
        RB_Write(&s_bt24_rx_ring, &s_bt24_dma_rx_buf[start], first);
        RB_Write(&s_bt24_rx_ring, &s_bt24_dma_rx_buf[0], len - first);
    }
}

/**
 * @brief  错误恢复：重启DMA接收并清空残留数据
 * @note   由 HAL_UART_ErrorCallback（bsp_uart.c）的 USART2 分支调用，
 *         IDLE 中断已在调用侧关闭，这里恢复后重新使能
 */
void BSP_BT24_UartErrorRecover(void)
{
    RB_Reset(&s_bt24_rx_ring);
    s_bt24_dma_last_pos = 0U;
    BSP_BT24_StartRxDma();
}

uint32_t BSP_BT24_Available(void)
{
    return RB_Used(&s_bt24_rx_ring);
}

uint32_t BSP_BT24_Read(uint8_t *buf, uint32_t len)
{
    return RB_Read(&s_bt24_rx_ring, buf, len);
}

void BSP_BT24_Send(const uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return;
    }
    /* 9600bps：每字节约 1.04ms，150B 长帧约 156ms，超时放宽到 500ms */
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)len, 500U);
}

void BSP_BT24_SendString(const char *str)
{
    uint32_t len = 0U;
    if (str == NULL) {
        return;
    }
    while (str[len] != '\0') {
        len++;
    }
    BSP_BT24_Send((const uint8_t *)str, len);
}

void BSP_BT24_Printf(const char *fmt, ...)
{
    char    buf[160];
    va_list args;
    int     n;

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        BSP_BT24_Send((const uint8_t *)buf, (uint32_t)n);
    }
}

void BSP_BT24_SendAT(const char *at_cmd)
{
    if (at_cmd == NULL) {
        return;
    }
    BSP_BT24_SendString(at_cmd);
    BSP_BT24_Send((const uint8_t *)"\r\n", 2U);
}

#endif /* BSP_BT24_ENABLE */
