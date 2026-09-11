/**
 ******************************************************************************
 * @file    protocol.h
 * @brief   协议层头文件 — 字符指令解析（行协议）
 *
 *  帧格式：
 *    - 指令以 '\n' 结尾（兼容 "\r\n"），支持大小写不敏感匹配
 *    - 当前支持指令（可在 s_cmd_table 中扩展）：
 *        LED1ON / LED1OFF / LED2ON / LED2OFF / ALLON / ALLOFF / HELP / DIST?
 *
 *  职责边界：
 *    - 协议层只负责"文本 -> 指令ID"的解析与匹配，不关心硬件动作
 *    - 指令对应的执行动作由 APP 层负责（app_cmd.c），保持解耦
 ******************************************************************************
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PROTOCOL_LINE_MAX_LEN   32   /*!< 单条指令最大长度（含结尾，不含'\0'） */

/** @brief 指令ID — 协议层输出的语义化指令 */
typedef enum {
    PROTOCOL_CMD_NONE = 0,      /*!< 未识别 */
    PROTOCOL_CMD_LED1_ON,       /*!< LED1点亮 */
    PROTOCOL_CMD_LED1_OFF,      /*!< LED1熄灭 */
    PROTOCOL_CMD_LED2_ON,       /*!< LED2点亮 */
    PROTOCOL_CMD_LED2_OFF,      /*!< LED2熄灭 */
    PROTOCOL_CMD_LED_ALL_ON,    /*!< 全部点亮 */
    PROTOCOL_CMD_LED_ALL_OFF,   /*!< 全部熄灭 */
    PROTOCOL_CMD_HELP,          /*!< 帮助 */
    PROTOCOL_CMD_DIST_QUERY,    /*!< 查询超声波距离 */
} protocol_cmd_t;

/** @brief 解析结果 */
typedef enum {
    PROTOCOL_RET_BUSY = 0,      /*!< 解析中，无完整帧 */
    PROTOCOL_RET_FRAME,         /*!< 完成一帧且匹配到指令 */
    PROTOCOL_RET_UNKNOWN,       /*!< 完成一帧但指令未识别 */
    PROTOCOL_RET_OVERFLOW,      /*!< 帧超长，已丢弃 */
} protocol_ret_t;

/** @brief 行解析器（状态由调用方持有，支持流式逐字节喂入） */
typedef struct {
    char     line[PROTOCOL_LINE_MAX_LEN];  /*!< 行缓冲 */
    uint16_t len;                          /*!< 当前已累积长度 */
} protocol_parser_t;

/**
 * @brief  初始化解析器
 * @param  parser 解析器对象
 */
void Protocol_Init(protocol_parser_t *parser);

/**
 * @brief  逐字节喂入数据，状态机式解析
 * @param  parser   解析器对象
 * @param  byte     输入字节
 * @param  cmd_out  输出：匹配到的指令ID（仅当返回 PROTOCOL_RET_FRAME 时有效）
 * @return 解析结果，见 protocol_ret_t
 */
protocol_ret_t Protocol_Feed(protocol_parser_t *parser, uint8_t byte, protocol_cmd_t *cmd_out);

#endif /* PROTOCOL_H */
