/**
 ******************************************************************************
 * @file    app_cmd.h
 * @brief   应用层指令任务头文件 — 串口指令解析与执行（数据通路第4级）
 *
 *  职责：作为消费者，从 FreeRTOS 流缓冲读取数据，经协议层解析后
 *  执行对应动作并回显应答：
 *    FreeRTOS流缓冲 -> [CmdTask] -> Protocol解析 -> BSP_LED控制 -> 应答
 *
 *  UART1 与 UART3 各自拥有独立的命令处理任务，分别由
 *  APP_UART1_CMD_ENABLE / APP_UART3_CMD_ENABLE 条件编译控制。
 *
 *  APP_CMD_Exec() 为公共LED执行函数，供串口指令任务和WiFi远程控灯共用。
 ******************************************************************************
 */
#ifndef APP_CMD_H
#define APP_CMD_H

#include "protocol.h"

/* ==================== 公共LED执行接口（串口/WiFi共用） ==================== */
#if BSP_LED_ENABLE

/**
 * @brief  执行指令对应的LED动作（指令ID -> LED开关）
 * @param  cmd 协议层解析出的指令ID
 * @note   供 UART1/UART3 命令任务和 WiFi 远程控灯共用；
 *         若对应模块的LED开关未启用，函数内为空操作
 */
void APP_CMD_Exec(protocol_cmd_t cmd);

#endif /* BSP_LED_ENABLE */

/* ==================== UART1 指令任务 ==================== */
#if APP_UART1_CMD_ENABLE

/**
 * @brief  UART1指令处理任务入口（由 app_tasks.c 创建）
 * @param  argument 传入 FreeRTOS 流缓冲句柄 (StreamBufferHandle_t)
 */
void APP_CMD1_Task(void *argument);

#endif /* APP_UART1_CMD_ENABLE */

/* ==================== UART3 指令任务 ==================== */
#if APP_UART3_CMD_ENABLE

/**
 * @brief  UART3指令处理任务入口（由 app_tasks.c 创建）
 * @param  argument 传入 FreeRTOS 流缓冲句柄 (StreamBufferHandle_t)
 */
void APP_CMD3_Task(void *argument);

#endif /* APP_UART3_CMD_ENABLE */

#endif /* APP_CMD_H */
