/**
 ******************************************************************************
 * @file    app_uart.c
 * @brief   应用层串口模块实现 — UART数据转发任务
 *
 *  数据通路（第3级：FreeRTOS 流缓冲的生产者）：
 *    BSP_UARTx_Read() 从软件环形缓冲读取
 *        -> xStreamBufferSend() 写入 FreeRTOS 流缓冲
 *        -> CmdTask 从流缓冲消费
 *
 *  UART1 与 UART3 各自独立：独立转发任务、独立流缓冲、独立发送函数，
 *  分别由 APP_UART1_CMD_ENABLE / APP_UART3_CMD_ENABLE 条件编译包裹。
 *
 *  设计说明：
 *    - 本任务不解析任何内容，纯搬运，职责单一
 *    - 无数据时 osDelay(1) 让出CPU；大数据量场景可改为
 *      由IDLE中断发送任务通知（见 bsp_uart 注释）以降低轮询开销
 ******************************************************************************
 */
#include "module_cfg.h"

#include "app_uart.h"
#include "bsp_uart.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include <string.h>

#define APP_UART_RELAY_CHUNK   64   /*!< 单次搬运块大小 */

/* ==========================================================================
 *  UART1 转发任务 + 发送函数
 * ========================================================================== */
#if APP_UART1_CMD_ENABLE

void APP_UART1_RelayTask(void *argument)
{
    StreamBufferHandle_t stream = (StreamBufferHandle_t)argument;
    uint8_t tmp[APP_UART_RELAY_CHUNK];
    uint32_t n;

    for (;;) {
        n = BSP_UART1_Read(tmp, sizeof(tmp));
        if (n > 0U) {
            xStreamBufferSend(stream, tmp, n, 0U);
        } else {
            osDelay(1);   /* 无数据，让出CPU */
        }
    }
}

void APP_UART1_SendString(const char *str)
{
    BSP_UART1_SendString(str);
}

void APP_UART1_SendRaw(const uint8_t *buf, uint32_t len)
{
    BSP_UART1_Send(buf, len);
}

#endif /* APP_UART1_CMD_ENABLE */

/* ==========================================================================
 *  UART3 转发任务 + 发送函数（与UART1同规格，独立缓冲区与通路）
 * ========================================================================== */
#if APP_UART3_CMD_ENABLE

void APP_UART3_RelayTask(void *argument)
{
    StreamBufferHandle_t stream = (StreamBufferHandle_t)argument;
    uint8_t tmp[APP_UART_RELAY_CHUNK];
    uint32_t n;

    for (;;) {
        n = BSP_UART3_Read(tmp, sizeof(tmp));
        if (n > 0U) {
            xStreamBufferSend(stream, tmp, n, 0U);
        } else {
            osDelay(1);   /* 无数据，让出CPU */
        }
    }
}

void APP_UART3_SendString(const char *str)
{
    BSP_UART3_SendString(str);
}

void APP_UART3_SendRaw(const uint8_t *buf, uint32_t len)
{
    BSP_UART3_Send(buf, len);
}

#endif /* APP_UART3_CMD_ENABLE */
