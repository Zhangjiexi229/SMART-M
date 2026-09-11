/**
 ******************************************************************************
 * @file    bsp_uart.c
 * @brief   UART板级驱动实现 — UART1/UART3 DMA循环接收 + IDLE判帧 + 软件环形缓冲
 *
 *  数据通路（BSP层）：
 *    USARTx RX --DMA循环--> [DMA环形缓冲] --IDLE中断判帧--> [软件环形缓冲]
 *
 *  关键点：
 *    1. DMA循环模式：HAL_UART_Receive_DMA() 一次性配置，硬件持续接收
 *    2. IDLE判帧：USARTx空闲中断，通过 __HAL_DMA_GET_COUNTER 计算
 *       本次新收到的字节数，环形拷贝进软件环形缓冲
 *    3. ISR内仅做"搬数据"这一件事，不阻塞、不调用RTOS API，
 *       BSP层保持RTOS无关，可单独移植
 *    4. 溢出/错误自动恢复：重写 HAL_UART_ErrorCallback，重启DMA接收
 *
 *  缓冲区独立性：
 *    UART1 使用 s_uart1_dma_rx_buf / s_uart1_ring_buf / s_uart1_rx_ring /
 *    s_uart1_dma_last_pos；UART3 使用 s_uart3_dma_rx_buf / s_uart3_ring_buf /
 *    s_uart3_rx_ring / s_uart3_dma_last_pos。两者完全独立，不可共用：
 *    DMA硬件独立写入、ring_buffer_t 含 head/tail 状态，共用会导致数据
 *    混淆并破坏 SPSC 无锁模型。
 ******************************************************************************
 */
#include "module_cfg.h"

#include "bsp_uart.h"
#include "ring_buffer.h"
#include "main.h"
#include "usart.h"
#include "dma.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 *  UART1 驱动
 * ========================================================================== */
#if BSP_UART1_ENABLE

/* ========== 外部声明（不修改MX文件，直接在此声明） ========== */
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;

/* ========== 静态资源 ========== */
static uint8_t        s_uart1_dma_rx_buf[BSP_UART1_RX_DMA_BUF_SIZE];   /* DMA环形缓冲 */
static uint8_t        s_uart1_ring_buf[BSP_UART1_RX_RING_BUF_SIZE];    /* 软件环形缓冲 */
static ring_buffer_t  s_uart1_rx_ring;
static volatile uint32_t s_uart1_dma_last_pos;                         /* DMA缓冲已消费位置 */

/* ========== 内部函数 ========== */
static void BSP_UART1_StartRxDma(void);

/**
 * @brief  启动DMA循环接收并使能IDLE中断
 */
static void BSP_UART1_StartRxDma(void)
{
    HAL_UART_Receive_DMA(&huart1, s_uart1_dma_rx_buf, BSP_UART1_RX_DMA_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
}

void BSP_UART1_Init(void)
{
    RB_Init(&s_uart1_rx_ring, s_uart1_ring_buf, sizeof(s_uart1_ring_buf));
    s_uart1_dma_last_pos = 0U;
    BSP_UART1_StartRxDma();
}

/**
 * @brief  IDLE空闲中断处理（判帧）
 *
 *  原理：
 *    - DMA计数器(NDTR)递减计数，已收字节数 = 缓冲大小 - 当前计数值
 *    - 与上次消费位置做差，得到本次新数据长度（支持环形绕回）
 *    - 将新数据从 DMA 环形缓冲搬到软件环形缓冲，交给上层任务
 */
void BSP_UART1_RxIdleHandler(void)
{
    uint32_t pos   = BSP_UART1_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
    uint32_t start = s_uart1_dma_last_pos;
    uint32_t len;
    uint32_t first;

    if (pos >= start) {
        len = pos - start;
    } else {
        len = BSP_UART1_RX_DMA_BUF_SIZE - start + pos;   /* 环形绕回 */
    }
    s_uart1_dma_last_pos = pos;

    if (len == 0U) {
        return;
    }

    /* 从DMA环形缓冲拷贝到软件环形缓冲（可能跨缓冲末尾，分两段） */
    first = BSP_UART1_RX_DMA_BUF_SIZE - start;
    if (len <= first) {
        RB_Write(&s_uart1_rx_ring, &s_uart1_dma_rx_buf[start], len);
    } else {
        RB_Write(&s_uart1_rx_ring, &s_uart1_dma_rx_buf[start], first);
        RB_Write(&s_uart1_rx_ring, &s_uart1_dma_rx_buf[0], len - first);
    }
}

uint32_t BSP_UART1_Available(void)
{
    return RB_Used(&s_uart1_rx_ring);
}

uint32_t BSP_UART1_Read(uint8_t *buf, uint32_t len)
{
    return RB_Read(&s_uart1_rx_ring, buf, len);
}

void BSP_UART1_Send(const uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return;
    }

    /* 绕过 HAL_UART_Transmit，直接操作 USART1 寄存器，带硬件超时保护
     * （不依赖 uwTick，避免 SysTick 中断未触发时死等） */
    for (uint32_t i = 0U; i < len; i++) {
        /* 等 TXE 标志置位（DR 寄存器空），带循环计数超时 */
        uint32_t timeout = 500000U;
        while (!(USART1->SR & USART_SR_TXE)) {
            if (--timeout == 0U) {
                return;  /* 超时，放弃本次发送 */
            }
        }
        USART1->DR = buf[i];
    }

    /* 等 TC 标志置位（最后一个字节发送完成） */
    uint32_t timeout = 500000U;
    while (!(USART1->SR & USART_SR_TC)) {
        if (--timeout == 0U) {
            break;
        }
    }
}

void BSP_UART1_SendString(const char *str)
{
    uint32_t len = 0U;
    if (str == NULL) {
        return;
    }
    while (str[len] != '\0') {
        len++;
    }
    BSP_UART1_Send((const uint8_t *)str, len);
}

void BSP_UART1_Printf(const char *fmt, ...)
{
    char    buf[128];
    va_list args;
    int     n;

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        BSP_UART1_Send((const uint8_t *)buf, (uint32_t)n);
    }
}

#endif /* BSP_UART1_ENABLE */

/* ==========================================================================
 *  UART3 驱动（与UART1同配置，独立缓冲区）
 * ========================================================================== */
#if BSP_UART3_ENABLE

/* ========== 外部声明 ========== */
extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart3_rx;

/* ========== 静态资源（独立于UART1） ========== */
static uint8_t        s_uart3_dma_rx_buf[BSP_UART3_RX_DMA_BUF_SIZE];   /* UART3 DMA环形缓冲 */
static uint8_t        s_uart3_ring_buf[BSP_UART3_RX_RING_BUF_SIZE];    /* UART3 软件环形缓冲 */
static ring_buffer_t  s_uart3_rx_ring;
static volatile uint32_t s_uart3_dma_last_pos;                         /* UART3 DMA缓冲已消费位置 */

/* ========== 内部函数 ========== */
static void BSP_UART3_StartRxDma(void);

/**
 * @brief  启动UART3 DMA循环接收并使能IDLE中断
 */
static void BSP_UART3_StartRxDma(void)
{
    HAL_UART_Receive_DMA(&huart3, s_uart3_dma_rx_buf, BSP_UART3_RX_DMA_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
}

void BSP_UART3_Init(void)
{
    RB_Init(&s_uart3_rx_ring, s_uart3_ring_buf, sizeof(s_uart3_ring_buf));
    s_uart3_dma_last_pos = 0U;
    BSP_UART3_StartRxDma();
}

/**
 * @brief  UART3 IDLE空闲中断处理（判帧）
 */
void BSP_UART3_RxIdleHandler(void)
{
    uint32_t pos   = BSP_UART3_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
    uint32_t start = s_uart3_dma_last_pos;
    uint32_t len;
    uint32_t first;

    if (pos >= start) {
        len = pos - start;
    } else {
        len = BSP_UART3_RX_DMA_BUF_SIZE - start + pos;   /* 环形绕回 */
    }
    s_uart3_dma_last_pos = pos;

    if (len == 0U) {
        return;
    }

    /* 从DMA环形缓冲拷贝到软件环形缓冲（可能跨缓冲末尾，分两段） */
    first = BSP_UART3_RX_DMA_BUF_SIZE - start;
    if (len <= first) {
        RB_Write(&s_uart3_rx_ring, &s_uart3_dma_rx_buf[start], len);
    } else {
        RB_Write(&s_uart3_rx_ring, &s_uart3_dma_rx_buf[start], first);
        RB_Write(&s_uart3_rx_ring, &s_uart3_dma_rx_buf[0], len - first);
    }
}

uint32_t BSP_UART3_Available(void)
{
    return RB_Used(&s_uart3_rx_ring);
}

uint32_t BSP_UART3_Read(uint8_t *buf, uint32_t len)
{
    return RB_Read(&s_uart3_rx_ring, buf, len);
}

void BSP_UART3_Send(const uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return;
    }
    HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)len, 100U);
}

void BSP_UART3_SendString(const char *str)
{
    uint32_t len = 0U;
    if (str == NULL) {
        return;
    }
    while (str[len] != '\0') {
        len++;
    }
    BSP_UART3_Send((const uint8_t *)str, len);
}

void BSP_UART3_Printf(const char *fmt, ...)
{
    char    buf[128];
    va_list args;
    int     n;

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        BSP_UART3_Send((const uint8_t *)buf, (uint32_t)n);
    }
}

#endif /* BSP_UART3_ENABLE */

/* ==========================================================================
 *  公共：UART错误回调（重写HAL弱函数）
 *
 *  触发时机：接收溢出(ORE)、DMA错误等。此时HAL已停止接收并复位
 *  RxState=READY，这里统一重启DMA接收，并丢弃残留数据，恢复链路。
 * ========================================================================== */
#if BSP_UART1_ENABLE || BSP_UART3_ENABLE
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
#if BSP_UART1_ENABLE
    if (huart->Instance == USART1) {
        __HAL_UART_DISABLE_IT(&huart1, UART_IT_IDLE);
        RB_Reset(&s_uart1_rx_ring);
        s_uart1_dma_last_pos = 0U;
        BSP_UART1_StartRxDma();
    }
#endif /* BSP_UART1_ENABLE */

#if BSP_UART3_ENABLE
    if (huart->Instance == USART3) {
        __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE);
        RB_Reset(&s_uart3_rx_ring);
        s_uart3_dma_last_pos = 0U;
        BSP_UART3_StartRxDma();
    }
#endif /* BSP_UART3_ENABLE */
}
#endif /* BSP_UART1_ENABLE || BSP_UART3_ENABLE */
