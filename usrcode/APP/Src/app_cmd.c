/**
 ******************************************************************************
 * @file    app_cmd.c
 * @brief   应用层指令任务实现 — 串口指令解析与执行
 *  数据通路（第4级：FreeRTOS 流缓冲的消费者）：
 *    xStreamBufferReceive() 从流缓冲读取
 *        -> Protocol_Feed() 逐字节解析指令
 *        -> 执行LED动作（BSP_LED）+ UART应答
 *
 *  成帧策略（兼容不同串口助手）：
 *    - 标准帧：从'\n'（或 "\r\n"）结尾，到达即执行
 *    - 兼容帧：串口助手若未发送换行符，数据静默30ms 后按一帧强制执行
 *  UART1 与UART3 各自拥有独立的命令任务（APP_CMD1_Task / APP_CMD3_Task），
 *  分别由APP_UART1_CMD_ENABLE / APP_UART3_CMD_ENABLE 条件编译包裹。
 *  两者共用同一套解析核心（APP_CMD_Process）与LED执行逻辑（APP_CMD_Exec），
 *  通过发送函数指针区分应答输出到哪个串口。
 *  APP_CMD_Exec() 为公共函数，也供 WiFi 远程控灯（app_wifi.c）调用。
 *  职责边界：
 *    - 解析细节完全交给 Protocol 层，本文件只做指令ID -> 业务动作"映射
 *    - 新增指令：扩展protocol.c 指令表+ 本文件Exec/Reply 分支即可
 ******************************************************************************
 */
#include "module_cfg.h"

/* ==========================================================================
 *  公共 LED 执行函数（串口指令任务 + WiFi 远程控灯共用）
 *  只要 BSP_LED_ENABLE=1 即编译，，具体是否调用由各模块自己的开关决定
 * ========================================================================== */
#if BSP_LED_ENABLE

#include "app_cmd.h"
#include "protocol.h"
#include "bsp_led.h"
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
#include "app_eeprom.h"
#endif

/**
 * @brief  执行指令：指令ID -> LED 动作
 * @note   与具体串口无关，（UART1/UART3命令任务和WiFi远程控灯共用）
 *         LED动作执行一次即可，应答由调用方自行处理
 */
void APP_CMD_Exec(protocol_cmd_t cmd)
{
#if APP_UART1_LED_ENABLE || APP_UART3_LED_ENABLE || APP_WIFI_LED_ENABLE
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
    uint8_t led_state = APP_EEPROM_GetConfig()->led_state;
#endif
    switch (cmd) {
        case PROTOCOL_CMD_LED1_ON:
            BSP_LED_On(BSP_LED1);
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
            led_state |= 0x01U;
#endif
            break;
        case PROTOCOL_CMD_LED1_OFF:
            BSP_LED_Off(BSP_LED1);
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
            led_state &= ~0x01U;
#endif
            break;
        case PROTOCOL_CMD_LED2_ON:
            BSP_LED_On(BSP_LED2);
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
            led_state |= 0x02U;
#endif
            break;
        case PROTOCOL_CMD_LED2_OFF:
            BSP_LED_Off(BSP_LED2);
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
            led_state &= ~0x02U;
#endif
            break;
        case PROTOCOL_CMD_LED_ALL_ON:
            BSP_LED_On(BSP_LED1);
            BSP_LED_On(BSP_LED2);
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
            led_state |= 0x03U;
#endif
            break;
        case PROTOCOL_CMD_LED_ALL_OFF:
            BSP_LED_Off(BSP_LED1);
            BSP_LED_Off(BSP_LED2);
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
            led_state &= ~0x03U;
#endif
            break;
        default:
            break;
    }
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
    /* 同步保存LED状态到EEPROM（掉电保持），仅LED相关指令触发 */
    if ((cmd >= PROTOCOL_CMD_LED1_ON) && (cmd <= PROTOCOL_CMD_LED_ALL_OFF)) {
        (void)APP_EEPROM_SetLEDStateAll(led_state);
    }
#endif
#else
    (void)cmd;   /* 所有模块的LED控制均未启用，命令空操作 */
#endif
}

#endif /* BSP_LED_ENABLE */

/* ==========================================================================
 *  串口指令任务（UART1 / UART3）
 * ========================================================================== */
#if APP_UART1_CMD_ENABLE || APP_UART3_CMD_ENABLE

#include "app_cmd.h"
#include "app_uart.h"
#include "bsp_uart.h"
#include "protocol.h"
#include "protocol_hex.h"
#include "app_hex_cmd.h"
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
#include "app_hc_sr04.h"
#endif
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include <stdio.h>

#define APP_CMD_CHUNK             64   /*!< 单次从流缓冲读取块大小 */
#define APP_CMD_FRAME_TIMEOUT_MS  30   /*!< 无换行符时，静默该时长视为一帧结束 */

#if APP_HEX_CMD_DEBUG
/**
 * @brief  以十六进制文本打印一帧字节hex数据（调试用，文本模式下可读） */
static void print_hex_frame(const char *tag, const uint8_t *frame)
{
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "[Hex] %s: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                     tag, frame[0], frame[1], frame[2], frame[3],
                     frame[4], frame[5], frame[6], frame[7]);
    if (n > 0) {
        BSP_UART1_SendString(buf);
    }
}
#endif

/** @brief 串口发送函数指针类型（用于区分UART1/UART3应答输出）*/
typedef void (*app_cmd_send_fn_t)(const char *);
typedef void (*app_cmd_send_raw_fn_t)(const uint8_t *, uint32_t);

static void APP_CMD_Process(StreamBufferHandle_t stream,
                            app_cmd_send_fn_t send_str,
                            app_cmd_send_raw_fn_t send_raw);
static void APP_CMD_Reply(protocol_cmd_t cmd, app_cmd_send_fn_t send_str);

/* ---- UART1 命令任务入口 ---- */
#if APP_UART1_CMD_ENABLE
void APP_CMD1_Task(void *argument)
{
    APP_CMD_Process((StreamBufferHandle_t)argument, APP_UART1_SendString, APP_UART1_SendRaw);
}
#endif /* APP_UART1_CMD_ENABLE */

/* ---- UART3 命令任务入口 ---- */
#if APP_UART3_CMD_ENABLE
void APP_CMD3_Task(void *argument)
{
    APP_CMD_Process((StreamBufferHandle_t)argument, APP_UART3_SendString, APP_UART3_SendRaw);
}
#endif /* APP_UART3_CMD_ENABLE */

/**
 * @brief  通用指令处理循环：从流缓冲读取-> 文本/二进制协议解析-> 执行 -> 应答
 * @note   同时支持文本协议（LED1ON等）和字节二进制协议，自动识别帧头
 */
static void APP_CMD_Process(StreamBufferHandle_t stream,
                            app_cmd_send_fn_t send_str,
                            app_cmd_send_raw_fn_t send_raw)
{
    protocol_parser_t parser;
    proto_hex_parser_t hex_parser;
    uint8_t buf[APP_CMD_CHUNK];
    size_t  n;
    uint32_t i;

    Protocol_Init(&parser);
    ProtoHex_Init(&hex_parser);

    send_str("\r\nSmartEnv v1.0 ready\r\n");
    send_str("Text: LED1ON/LED1OFF/LED2ON/LED2OFF/ALLON/ALLOFF/DIST?/HELP\r\n");
    send_str("Hex: 8-byte frame (ver=1, dev=1, CRC8)\r\n");

    for (;;) {
        TickType_t wait = (parser.len > 0U) ? pdMS_TO_TICKS(APP_CMD_FRAME_TIMEOUT_MS)
                                            : portMAX_DELAY;
        n = xStreamBufferReceive(stream, buf, sizeof(buf), wait);

        if (n == 0U) {
            /* 文本协议超时强制成帧 */
            if (parser.len > 0U) {
                protocol_cmd_t cmd = PROTOCOL_CMD_NONE;
                protocol_ret_t ret = Protocol_Feed(&parser, (uint8_t)'\n', &cmd);
                if (ret == PROTOCOL_RET_FRAME) {
#if BSP_LED_ENABLE
                    APP_CMD_Exec(cmd);
#endif
                    APP_CMD_Reply(cmd, send_str);
                } else if (ret == PROTOCOL_RET_UNKNOWN) {
                    send_str("ERR: unknown cmd, type HELP\r\n");
                }
            }
            continue;
        }

        for (i = 0U; i < n; i++) {
            /* ---- 十六进制协议解析（固定8字节，自动识别帧头） ---- */
            uint8_t hex_frame[PROTO_HEX_FRAME_SIZE];
            proto_hex_ret_t hret = ProtoHex_Feed(&hex_parser, buf[i], hex_frame);
            if (hret == PROTO_HEX_RET_FRAME) {
                uint8_t reply[PROTO_HEX_FRAME_SIZE];
#if APP_HEX_CMD_DEBUG
                print_hex_frame("RX", hex_frame);
#endif
                if (APP_HexCmd_Process(hex_frame, reply) != 0U) {
#if APP_HEX_CMD_DEBUG
                    print_hex_frame("TX", reply);
#endif
                    send_raw(reply, PROTO_HEX_FRAME_SIZE);
                }
                continue;  /* 十六进制帧不喂入文本解析器 */
            } else if (hret == PROTO_HEX_RET_CRC_ERROR) {
                send_str("ERR: hex frame CRC error\r\n");
                continue;
            }
            /* 正在接收十六进制帧时(len>0)，该字节属于十六进制帧，不喂文本解析器；
               len==0 说明该字节不是十六进制帧头，交给文本协议处理 */
            if (hex_parser.len > 0U) {
                continue;
            }

            /* ---- 文本协议解析 ---- */
            protocol_cmd_t cmd = PROTOCOL_CMD_NONE;
            protocol_ret_t ret = Protocol_Feed(&parser, buf[i], &cmd);

            if (ret == PROTOCOL_RET_FRAME) {
#if BSP_LED_ENABLE
                APP_CMD_Exec(cmd);
#endif
                APP_CMD_Reply(cmd, send_str);
            } else if (ret == PROTOCOL_RET_UNKNOWN) {
                send_str("ERR: unknown cmd, type HELP\r\n");
            } else if (ret == PROTOCOL_RET_OVERFLOW) {
                send_str("ERR: cmd too long\r\n");
            }
        }
    }
}

/**
 * @brief  指令应答回显（通过指定串口发送）
 */
static void APP_CMD_Reply(protocol_cmd_t cmd, app_cmd_send_fn_t send_str)
{
    switch (cmd) {
        case PROTOCOL_CMD_LED1_ON:
            send_str("LED1 ON\r\n");
            break;
        case PROTOCOL_CMD_LED1_OFF:
            send_str("LED1 OFF\r\n");
            break;
        case PROTOCOL_CMD_LED2_ON:
            send_str("LED2 ON\r\n");
            break;
        case PROTOCOL_CMD_LED2_OFF:
            send_str("LED2 OFF\r\n");
            break;
        case PROTOCOL_CMD_LED_ALL_ON:
            send_str("ALL ON\r\n");
            break;
        case PROTOCOL_CMD_LED_ALL_OFF:
            send_str("ALL OFF\r\n");
            break;
        case PROTOCOL_CMD_HELP:
            send_str("CMDS: LED1ON LED1OFF LED2ON LED2OFF ALLON ALLOFF DIST? HELP\r\n");
            break;
        case PROTOCOL_CMD_DIST_QUERY: {
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
            HC_SR04_Snapshot_t us;
            char buf[32];
            APP_HC_SR04_GetSnapshot(&us);
            if (us.valid) {
                int len = snprintf(buf, sizeof(buf), "DIST=%umm\r\n", (unsigned)us.distance_mm);
                if (len > 0) send_str(buf);
            } else {
                send_str("DIST=invalid\r\n");
            }
#else
            send_str("DIST=disabled\r\n");
#endif
            break;
        }
        default:
            break;
    }
}

#endif /* APP_UART1_CMD_ENABLE || APP_UART3_CMD_ENABLE */
