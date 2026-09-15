/**
 ******************************************************************************
 * @file    bsp_bt24.h
 * @brief   DX-BT24 BLE串口透传模块板级驱动头文件 — 基于 USART2(PA2/PA3)
 *
 *  硬件接线（BT24 模块 ↔ STM32F407VET6）：
 *    BT24 UART-TX (pin1)  ->  PA3 (USART2_RX)
 *    BT24 UART-RX (pin2)  ->  PA2 (USART2_TX)
 *    BT24 VBAT   (pin12)  ->  3.3V
 *    BT24 GND    (pin13/14) ->  GND
 *
 *  模块默认参数：9600bps / 8 / N / 1，BLE Service UUID=FFE0，
 *  Notify/Write UUID=FFE1，Write UUID=FFE2，未连接=AT命令模式，
 *  被连接后自动进入透传模式。
 *
 *  数据通路（与 bsp_uart 的 UART1/UART3 同构，独立缓冲区）：
 *    USART2 RX --DMA循环--> [DMA环形缓冲] --IDLE中断判帧--> [软件环形缓冲]
 *
 *  集成点：
 *    - BSP_BT24_Init()            -> APP_BT24_Init()（APP_Init 中调用）
 *    - BSP_BT24_RxIdleHandler()   -> USART2_IRQHandler（stm32f4xx_it.c）
 *    - BSP_BT24_UartErrorRecover()-> HAL_UART_ErrorCallback（bsp_uart.c 中
 *                                    对 USART2 分支统一处理，避免重复定义弱函数）
 ******************************************************************************
 */
#ifndef BSP_BT24_H
#define BSP_BT24_H

#include <stdint.h>

/* ========== BT24 缓冲配置（独立于 UART1/UART3） ========== */
#define BSP_BT24_RX_DMA_BUF_SIZE   256   /*!< DMA接收环形缓冲大小（字节） */
#define BSP_BT24_RX_RING_BUF_SIZE  512   /*!< 软件环形缓冲大小（字节） */

/**
 * @brief  初始化 BT24 链路：启动 USART2 DMA循环接收 + 使能IDLE中断
 * @note   在任务调度器启动前调用（APP_Init -> APP_BT24_Init）
 */
void BSP_BT24_Init(void);

/**
 * @brief  IDLE空闲中断处理（判帧）：DMA环形缓冲 -> 软件环形缓冲
 * @note   由 USART2_IRQHandler 中的 USER CODE 区域调用
 */
void BSP_BT24_RxIdleHandler(void);

/**
 * @brief  错误恢复（DMA/ORE 溢出后重启接收，清空残留数据）
 * @note   由 HAL_UART_ErrorCallback（bsp_uart.c）中的 USART2 分支调用
 */
void BSP_BT24_UartErrorRecover(void);

/**
 * @brief  查询软件环形缓冲可读字节数
 * @return 可读字节数
 */
uint32_t BSP_BT24_Available(void);

/**
 * @brief  从软件环形缓冲读取数据（消费者：APP_BT24 任务）
 * @param  buf 目的缓冲区
 * @param  len 请求字节数
 * @return 实际读取字节数
 */
uint32_t BSP_BT24_Read(uint8_t *buf, uint32_t len);

/**
 * @brief  阻塞发送一帧数据（任务上下文调用）
 * @note   9600bps 下每字节约 1.04ms，内部超时按 500ms 放宽，防止长帧被截断
 * @param  buf 源数据
 * @param  len 字节数
 */
void BSP_BT24_Send(const uint8_t *buf, uint32_t len);

/**
 * @brief  发送字符串
 * @param  str 以'\0'结尾的字符串
 */
void BSP_BT24_SendString(const char *str);

/**
 * @brief  格式化输出到 BT24（透传通道）
 * @param  fmt printf风格格式串
 */
void BSP_BT24_Printf(const char *fmt, ...);

/**
 * @brief  发送一条 AT 指令（自动追加 \r\n）
 * @param  at_cmd 不含\r\n的AT命令，如 "AT+NOTI1"、"AT+NAMEBT24"
 * @note   仅在模块处于 AT 命令模式（未连接）时有效
 */
void BSP_BT24_SendAT(const char *at_cmd);

#endif /* BSP_BT24_H */
