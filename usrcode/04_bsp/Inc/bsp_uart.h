/**
 ******************************************************************************
 * @file    bsp_uart.h
 * @brief   UART板级驱动头文件 — UART1/UART3 DMA环形接收 + IDLE判帧 + 软件环形缓冲
 *
 *  数据通路职责（本层负责前两级）：
 *    1. DMA环形缓冲：HAL_UART_Receive_DMA 循环模式，硬件自动收数
 *    2. IDLE判帧：USARTx 总线空闲中断判定"一帧结束"，将 DMA 环形缓冲
 *       中新到的字节搬入软件环形缓冲（bsp_ring_buffer），ISR内不阻塞
 *    后续由 APP 层任务读取软件环形缓冲 → 写入 FreeRTOS 流缓冲
 *
 *  缓冲区说明：UART1 与 UART3 各自拥有独立的 DMA 缓冲和软件环形缓冲，
 *  不可共用——DMA硬件独立写入、ring_buffer_t 含 head/tail 状态，
 *  共用会导致数据混淆并破坏 SPSC 无锁模型。
 *
 *  集成点：
 *    - BSP_UART1_Init()            -> APP_Init()（main.c USER CODE 2）
 *    - BSP_UART1_RxIdleHandler()   -> USART1_IRQHandler（stm32f4xx_it.c）
 *    - BSP_UART3_Init()            -> APP_Init()（main.c USER CODE 2）
 *    - BSP_UART3_RxIdleHandler()   -> USART3_IRQHandler（stm32f4xx_it.c）
 *    - HAL_UART_ErrorCallback()    -> 本文件内重写（DMA错误/溢出自动恢复）
 ******************************************************************************
 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

/* ========== UART1 缓冲配置 ========== */
#define BSP_UART1_RX_DMA_BUF_SIZE   256   /*!< DMA接收环形缓冲大小（字节） */
#define BSP_UART1_RX_RING_BUF_SIZE  512   /*!< 软件环形缓冲大小（字节） */

/* ========== UART3 缓冲配置（与UART1同规格，独立缓冲区） ========== */
#define BSP_UART3_RX_DMA_BUF_SIZE   256   /*!< UART3 DMA接收环形缓冲大小（字节） */
#define BSP_UART3_RX_RING_BUF_SIZE  512   /*!< UART3 软件环形缓冲大小（字节） */

/* ==================== UART1 API ==================== */

/**
 * @brief  初始化UART1：启动DMA循环接收 + 使能IDLE中断
 * @note   在任务调度器启动前调用（APP_Init）
 */
void BSP_UART1_Init(void);

/**
 * @brief  IDLE空闲中断处理（判帧）：DMA环形缓冲 -> 软件环形缓冲
 * @note   由 USART1_IRQHandler 中的 USER CODE 区域调用
 */
void BSP_UART1_RxIdleHandler(void);

/**
 * @brief  查询软件环形缓冲可读字节数
 * @return 可读字节数
 */
uint32_t BSP_UART1_Available(void);

/**
 * @brief  从软件环形缓冲读取数据（消费者：APP转发任务）
 * @param  buf 目的缓冲区
 * @param  len 请求字节数
 * @return 实际读取字节数
 */
uint32_t BSP_UART1_Read(uint8_t *buf, uint32_t len);

/**
 * @brief  阻塞发送一帧数据（任务上下文调用）
 * @param  buf 源数据
 * @param  len 字节数
 */
void BSP_UART1_Send(const uint8_t *buf, uint32_t len);

/**
 * @brief  发送字符串
 * @param  str 以'\0'结尾的字符串
 */
void BSP_UART1_SendString(const char *str);

/**
 * @brief  格式化输出到UART1
 * @param  fmt printf风格格式串
 */
void BSP_UART1_Printf(const char *fmt, ...);

/* ==================== UART3 API ==================== */

/**
 * @brief  初始化UART3：启动DMA循环接收 + 使能IDLE中断
 * @note   在任务调度器启动前调用（APP_Init）
 */
void BSP_UART3_Init(void);

/**
 * @brief  IDLE空闲中断处理（判帧）：DMA环形缓冲 -> 软件环形缓冲
 * @note   由 USART3_IRQHandler 中的 USER CODE 区域调用
 */
void BSP_UART3_RxIdleHandler(void);

/**
 * @brief  查询软件环形缓冲可读字节数
 * @return 可读字节数
 */
uint32_t BSP_UART3_Available(void);

/**
 * @brief  从软件环形缓冲读取数据（消费者：APP转发任务）
 * @param  buf 目的缓冲区
 * @param  len 请求字节数
 * @return 实际读取字节数
 */
uint32_t BSP_UART3_Read(uint8_t *buf, uint32_t len);

/**
 * @brief  阻塞发送一帧数据（任务上下文调用）
 * @param  buf 源数据
 * @param  len 字节数
 */
void BSP_UART3_Send(const uint8_t *buf, uint32_t len);

/**
 * @brief  发送字符串
 * @param  str 以'\0'结尾的字符串
 */
void BSP_UART3_SendString(const char *str);

/**
 * @brief  格式化输出到UART3
 * @param  fmt printf风格格式串
 */
void BSP_UART3_Printf(const char *fmt, ...);

#endif /* BSP_UART_H */
