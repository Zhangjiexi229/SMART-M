/**
 ******************************************************************************
 * @file    protocol_hex.h
 * @brief   8字节固定帧十六进制通信协议头文件
 *
 *  帧格式（固定8字节，大端序）：
 *    B0: [7:4]版本(=0x1)  [3:0]设备地址(0~15, 0=广播)
 *    B1: 功能码 CMD
 *    B2: 寄存器地址高8位 REG_H
 *    B3: 寄存器地址低8位 REG_L
 *    B4: 数据高8位 DATA_H
 *    B5: 数据低8位 DATA_L
 *    B6: [7]应答标志(1=应答帧)  [6:0]帧序号(0~127)
 *    B7: CRC8（前7字节，poly=0x07, init=0x00）
 *
 *  设计目标：
 *    - 类Modbus寄存器模型，后续可直接映射Modbus RTU/TCP
 *    - 寄存器地址可映射MQTT topic（如 dev/01/reg/0010）
 *    - 固定帧长，解析开销极低，适合串口/UDP/TCP透传
 *    - 与文本协议并存：B0高4位=0x1(0x10~0x1F)为十六进制帧，其余为文本帧
 ******************************************************************************
 */
#ifndef PROTOCOL_HEX_H
#define PROTOCOL_HEX_H

#include <stdint.h>

/* ========== 协议常量 ========== */
#define PROTO_HEX_FRAME_SIZE     8U
#define PROTO_HEX_VERSION        0x1U
#define PROTO_HEX_DEVICE_ADDR    0x1U    /* 默认设备地址 */
#define PROTO_HEX_BROADCAST_ADDR 0x0U    /* 广播地址 */

/* ========== 功能码 ========== */
typedef enum {
    PROTO_HEX_CMD_READ_REG    = 0x01,  /* 读寄存器：REG=地址，应答DATA=值 */
    PROTO_HEX_CMD_WRITE_REG   = 0x02,  /* 写寄存器：REG=地址, DATA=值 */
    PROTO_HEX_CMD_LED_CTRL    = 0x10,  /* LED控制：DATA bit0~3=LED1~4 */
    PROTO_HEX_CMD_SENSOR_REPORT = 0x11, /* 传感器主动上报：REG=类型, DATA=值 */
    PROTO_HEX_CMD_HEARTBEAT   = 0x12,  /* 心跳：应答DATA=运行时间低16位 */
    PROTO_HEX_CMD_ACK         = 0xEE,  /* 通用成功应答：DATA=原功能码 */
    PROTO_HEX_CMD_ERROR       = 0xFF,  /* 错误应答：DATA=错误码 */
} proto_hex_cmd_t;

/* ========== 错误码 ========== */
#define PROTO_HEX_ERR_INVALID_CMD   0x01  /* 非法功能码 */
#define PROTO_HEX_ERR_INVALID_REG   0x02  /* 非法寄存器地址 */
#define PROTO_HEX_ERR_INVALID_VALUE 0x03  /* 非法数据值 */
#define PROTO_HEX_ERR_CRC           0x04  /* CRC校验失败 */

/* ========== 寄存器地址映射 ========== */
#define REG_DEVICE_ID        0x0000U  /* 设备ID (固定0x0001) */
#define REG_FW_VERSION       0x0001U  /* 固件版本 高8主版本 低8次版本 */
#define REG_PROTO_VERSION    0x0002U  /* 协议版本 (固定0x0100) */
#define REG_RUN_STATUS       0x0003U  /* 运行状态 bit0=WiFi bit1=TCP bit2=传感器 */
#define REG_LED_STATE        0x0010U  /* LED状态 bit0~3=LED1~4 */
#define REG_LED_ENABLE_MASK  0x0011U  /* LED使能掩码 */
#define REG_TEMPERATURE      0x0020U  /* 温度 int16, 单位0.1°C */
#define REG_HUMIDITY         0x0021U  /* 湿度 uint16, 单位0.1% */
#define REG_LIGHT_ADC        0x0022U  /* 光照ADC值 */
#define REG_LIGHT_LEVEL      0x0023U  /* 光照等级 0=BRIGHT 1=MEDIUM 2=DARK */
#define REG_ULTRASONIC_DIST  0x0024U  /* 超声波距离(mm, 只读, 0=无效) */
#define REG_RUNTIME_LOW      0x0100U  /* 运行时间低16位(秒) */
#define REG_RUNTIME_HIGH     0x0101U  /* 运行时间高16位(秒) */
#define REG_REBOOT_COUNT     0x0102U  /* 重启次数 */
#define REG_WIFI_SSID_BASE   0x0200U  /* WiFi SSID(每寄存器2字符, 共16寄存器=32字节) */
#define REG_WIFI_PWD_BASE    0x0210U  /* WiFi密码(每寄存器2字符, 共32寄存器=64字节) */
#define REG_TCP_IP_HIGH      0x0230U  /* TCP服务器IP高16位 */
#define REG_TCP_IP_LOW       0x0231U  /* TCP服务器IP低16位 */
#define REG_TCP_PORT         0x0232U  /* TCP服务器端口 */
#define REG_REPORT_PERIOD    0x0233U  /* 上报周期(ms, 最小500) */
#define REG_LIGHT_OFFSET     0x0240U  /* 光照校准偏移 */
#define REG_TEMP_OFFSET      0x0241U  /* 温度校准偏移(0.1°C) */
#define REG_ULTRASONIC_PERIOD    0x0250U  /* 超声波测量周期(ms, 最小100, 保存EEPROM) */
#define REG_ULTRASONIC_THRESHOLD 0x0251U /* 超声波报警阈值(mm, 保存EEPROM) */
#define REG_ULTRASONIC_ENABLE    0x0252U /* 超声波使能(0/1, 保存EEPROM) */
#define REG_RESET_DEFAULT    0xFF00U  /* 恢复默认: 写0xA5A5触发 */

/* ========== 解析结果 ========== */
typedef enum {
    PROTO_HEX_RET_BUSY = 0,    /* 收集中，未完成 */
    PROTO_HEX_RET_FRAME,       /* 完整帧且CRC正确 */
    PROTO_HEX_RET_CRC_ERROR,   /* 完整帧但CRC错误 */
} proto_hex_ret_t;

/** @brief 十六进制协议解析器（状态由调用方持有） */
typedef struct {
    uint8_t buf[PROTO_HEX_FRAME_SIZE];
    uint8_t len;
    uint8_t tx_seq;   /* 发送帧序号计数器 */
} proto_hex_parser_t;

/* ========== API ========== */

/**
 * @brief  初始化解析器
 */
void ProtoHex_Init(proto_hex_parser_t *parser);

/**
 * @brief  逐字节喂入，检测8字节十六进制帧
 * @param  parser    解析器
 * @param  byte      输入字节
 * @param  frame_out 输出：完整帧时复制8字节到此缓冲区（需≥8字节）
 * @retval 解析结果
 * @note   仅当 B0 高4位==PROTO_HEX_VERSION 时才视为十六进制帧起始，
 *         其余字节被忽略（与文本协议共存时安全）
 */
proto_hex_ret_t ProtoHex_Feed(proto_hex_parser_t *parser, uint8_t byte, uint8_t *frame_out);

/**
 * @brief  构建8字节应答/请求帧
 * @param  buf       输出缓冲区（≥8字节）
 * @param  dev_addr  设备地址
 * @param  cmd       功能码
 * @param  reg       寄存器地址
 * @param  data      数据
 * @param  seq       帧序号(0~127)
 * @param  is_ack    1=应答帧(bit7=1), 0=请求帧
 * @retval 帧长度(固定8)
 */
uint8_t ProtoHex_BuildFrame(uint8_t *buf, uint8_t dev_addr, uint8_t cmd,
                            uint16_t reg, uint16_t data, uint8_t seq, uint8_t is_ack);

/**
 * @brief  CRC8 校验（poly=0x07, init=0x00）
 */
uint8_t ProtoHex_CRC8(const uint8_t *data, uint8_t len);

/* ========== 帧字段提取宏 ========== */
#define PROTO_HEX_GET_VERSION(f)   (((f)[0] >> 4) & 0x0F)
#define PROTO_HEX_GET_DEVADDR(f)   ((f)[0] & 0x0F)
#define PROTO_HEX_GET_CMD(f)       ((f)[1])
#define PROTO_HEX_GET_REG(f)       (((uint16_t)(f)[2] << 8) | (f)[3])
#define PROTO_HEX_GET_DATA(f)      (((uint16_t)(f)[4] << 8) | (f)[5])
#define PROTO_HEX_GET_SEQ(f)       ((f)[6] & 0x7F)
#define PROTO_HEX_IS_ACK(f)        (((f)[6] & 0x80) != 0U)

#endif /* PROTOCOL_HEX_H */
