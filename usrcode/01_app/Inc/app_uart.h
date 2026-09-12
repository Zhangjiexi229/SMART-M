/**
 ******************************************************************************
 * @file    app_uart.h
 * @brief   应用层串口模块头文件 — UART数据转发（数据通路第3级）
 *
 *  职责：作为生产者，把 BSP 软件环形缓冲中的数据搬到 FreeRTOS 流缓冲，
 *  实现"硬件驱动层"与"指令处理层"的解耦：
 *    BSP环形缓冲(ISR写入) -> [UartRelayTask] -> FreeRTOS流缓冲 -> CmdTask
 *
 *  UART1 与 UART3 各自拥有独立的转发任务、流缓冲和发送函数，
 *  分别由 APP_UART1_CMD_ENABLE / APP_UART3_CMD_ENABLE 条件编译控制。
 ******************************************************************************
 */
#ifndef APP_UART_H
#define APP_UART_H

#include <stdint.h>

/* ==================== UART1 指令链路 ==================== */
#if APP_UART1_CMD_ENABLE

/**
 * @brief  UART1转发任务入口（由 app_tasks.c 创建）
 * @param  argument 传入 FreeRTOS 流缓冲句柄 (StreamBufferHandle_t)
 */
void APP_UART1_RelayTask(void *argument);

/**
 * @brief  通过UART1发送字符串（供指令应答使用）
 * @param  str 以'\0'结尾的字符串
 */
void APP_UART1_SendString(const char *str);

/**
 * @brief  通过UART1发送原始二进制数据（供二进制协议应答使用）
 * @param  buf 数据缓冲区
 * @param  len 字节数
 */
void APP_UART1_SendRaw(const uint8_t *buf, uint32_t len);

#endif /* APP_UART1_CMD_ENABLE */

/* ==================== UART3 指令链路 ==================== */
#if APP_UART3_CMD_ENABLE

/**
 * @brief  UART3转发任务入口（由 app_tasks.c 创建）
 * @param  argument 传入 FreeRTOS 流缓冲句柄 (StreamBufferHandle_t)
 */
void APP_UART3_RelayTask(void *argument);

/**
 * @brief  通过UART3发送字符串（供指令应答使用）
 * @param  str 以'\0'结尾的字符串
 */
void APP_UART3_SendString(const char *str);

/**
 * @brief  通过UART3发送原始二进制数据（供二进制协议应答使用）
 * @param  buf 数据缓冲区
 * @param  len 字节数
 */
void APP_UART3_SendRaw(const uint8_t *buf, uint32_t len);

#endif /* APP_UART3_CMD_ENABLE */

#endif /* APP_UART_H */
