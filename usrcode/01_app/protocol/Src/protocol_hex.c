/**
 ******************************************************************************
 * @file    protocol_hex.c
 * @brief   8字节固定帧十六进制通信协议实现
 ******************************************************************************
 */
#include "module_cfg.h"
#if PROTOCOL_ENABLE

#include "protocol_hex.h"
#include <stddef.h>

void ProtoHex_Init(proto_hex_parser_t *parser)
{
    if (parser == NULL) return;
    parser->len = 0U;
    parser->tx_seq = 0U;
}

proto_hex_ret_t ProtoHex_Feed(proto_hex_parser_t *parser, uint8_t byte, uint8_t *frame_out)
{
    if ((parser == NULL) || (frame_out == NULL)) {
        return PROTO_HEX_RET_BUSY;
    }

    /* 帧起始检测：B0高4位必须是版本号(0x1)，否则忽略（与文本协议共存） */
    if (parser->len == 0U) {
        if (((byte >> 4) & 0x0FU) != PROTO_HEX_VERSION) {
            return PROTO_HEX_RET_BUSY;   /* 不是十六进制帧，忽略 */
        }
        parser->buf[0] = byte;
        parser->len = 1U;
        return PROTO_HEX_RET_BUSY;
    }

    /* 收集中 */
    parser->buf[parser->len] = byte;
    parser->len++;

    if (parser->len < PROTO_HEX_FRAME_SIZE) {
        return PROTO_HEX_RET_BUSY;
    }

    /* 收满8字节，校验CRC */
    parser->len = 0U;   /* 复位，准备下一帧 */
    if (ProtoHex_CRC8(parser->buf, PROTO_HEX_FRAME_SIZE - 1U) != parser->buf[PROTO_HEX_FRAME_SIZE - 1U]) {
        return PROTO_HEX_RET_CRC_ERROR;
    }

    /* CRC正确，输出帧 */
    for (uint8_t i = 0U; i < PROTO_HEX_FRAME_SIZE; i++) {
        frame_out[i] = parser->buf[i];
    }
    return PROTO_HEX_RET_FRAME;
}

uint8_t ProtoHex_BuildFrame(uint8_t *buf, uint8_t dev_addr, uint8_t cmd,
                            uint16_t reg, uint16_t data, uint8_t seq, uint8_t is_ack)
{
    if (buf == NULL) return 0U;

    buf[0] = (uint8_t)((PROTO_HEX_VERSION << 4) | (dev_addr & 0x0FU));
    buf[1] = cmd;
    buf[2] = (uint8_t)(reg >> 8);
    buf[3] = (uint8_t)(reg & 0xFFU);
    buf[4] = (uint8_t)(data >> 8);
    buf[5] = (uint8_t)(data & 0xFFU);
    buf[6] = (uint8_t)((is_ack ? 0x80U : 0x00U) | (seq & 0x7FU));
    buf[7] = ProtoHex_CRC8(buf, PROTO_HEX_FRAME_SIZE - 1U);

    return PROTO_HEX_FRAME_SIZE;
}

uint8_t ProtoHex_CRC8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00U;
    uint8_t i, j;

    if (data == NULL) return 0U;

    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (j = 0U; j < 8U; j++) {
            if (crc & 0x80U) {
                crc = (uint8_t)((crc << 1) ^ 0x07U);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

#endif /* PROTOCOL_ENABLE */
