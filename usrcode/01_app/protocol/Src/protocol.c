/**
 ******************************************************************************
 * @file    protocol.c
 * @brief   协议层实现 — 字符指令解析
 ******************************************************************************
 */
#include "module_cfg.h"
#if PROTOCOL_ENABLE

#include "protocol.h"
#include <ctype.h>

/* ========== 指令表 ========== */
typedef struct {
    const char     *name;   /*!< 指令字符串（ASCII，表驱动扩展） */
    protocol_cmd_t  cmd;    /*!< 对应指令ID */
} protocol_cmd_item_t;

static const protocol_cmd_item_t s_cmd_table[] = {
    { "LED1ON",  PROTOCOL_CMD_LED1_ON     },
    { "LED1OFF", PROTOCOL_CMD_LED1_OFF    },
    { "LED2ON",  PROTOCOL_CMD_LED2_ON     },
    { "LED2OFF", PROTOCOL_CMD_LED2_OFF    },
    { "ALLON",   PROTOCOL_CMD_LED_ALL_ON  },
    { "ALLOFF",  PROTOCOL_CMD_LED_ALL_OFF },
    { "HELP",    PROTOCOL_CMD_HELP        },
    { "DIST?",   PROTOCOL_CMD_DIST_QUERY  },
};
#define PROTOCOL_CMD_TABLE_SIZE (sizeof(s_cmd_table) / sizeof(s_cmd_table[0]))

/* ========== 内部函数 ========== */
static int Protocol_StrCaseCmp(const char *a, const char *b);
static protocol_cmd_t Protocol_Match(const char *line);

/**
 * @brief  大小写不敏感字符串比较
 */
static int Protocol_StrCaseCmp(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        char ca = (char)tolower((unsigned char)*a);
        char cb = (char)tolower((unsigned char)*b);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        a++;
        b++;
    }
    if (*a == *b) {
        return 0;
    }
    return (*a != '\0') ? 1 : -1;
}

/**
 * @brief  在指令表中匹配
 * @return 匹配到的指令ID；未命中返回 PROTOCOL_CMD_NONE
 */
static protocol_cmd_t Protocol_Match(const char *line)
{
    uint32_t i;

    /* 跳过行首空白 */
    while (*line != '\0') {
        if (!isspace((unsigned char)*line)) {
            break;
        }
        line++;
    }

    for (i = 0U; i < PROTOCOL_CMD_TABLE_SIZE; i++) {
        if (Protocol_StrCaseCmp(line, s_cmd_table[i].name) == 0) {
            return s_cmd_table[i].cmd;
        }
    }
    return PROTOCOL_CMD_NONE;
}

void Protocol_Init(protocol_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    parser->len = 0U;
    parser->line[0] = '\0';
}

protocol_ret_t Protocol_Feed(protocol_parser_t *parser, uint8_t byte, protocol_cmd_t *cmd_out)
{
    protocol_cmd_t cmd;

    if ((parser == NULL) || (cmd_out == NULL)) {
        return PROTOCOL_RET_BUSY;
    }

    /* 回车忽略（兼容 CRLF） */
    if (byte == (uint8_t)'\r') {
        return PROTOCOL_RET_BUSY;
    }

    /* 帧结束：解析当前累积行 */
    if (byte == (uint8_t)'\n') {
        parser->line[parser->len] = '\0';
        cmd = Protocol_Match(parser->line);
        parser->len = 0U;
        parser->line[0] = '\0';
        if (cmd != PROTOCOL_CMD_NONE) {
            *cmd_out = cmd;
            return PROTOCOL_RET_FRAME;
        }
        return PROTOCOL_RET_UNKNOWN;
    }

    /* 普通字符：跳过行首空白，累积到行缓冲 */
    if ((parser->len == 0U) && isspace((int)byte)) {
        return PROTOCOL_RET_BUSY;
    }
    if (parser->len < (uint16_t)(PROTOCOL_LINE_MAX_LEN - 1U)) {
        parser->line[parser->len] = (char)byte;
        parser->len++;
    } else {
        /* 行溢出：丢弃整行并复位，避免错误指令被截断执行 */
        parser->len = 0U;
        parser->line[0] = '\0';
        return PROTOCOL_RET_OVERFLOW;
    }
    return PROTOCOL_RET_BUSY;
}

#endif /* PROTOCOL_ENABLE */
