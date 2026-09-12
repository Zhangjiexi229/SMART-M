/**
 ******************************************************************************
 * @file    app_hex_cmd.h
 * @brief   十六进制协议指令执行层 — 寄存器读写 + LED控制 + 配置修改 + EEPROM保存
 *
 *  本模块为公共执行层，供串口1、串口3、WiFi远程控灯共用。
 *  输入一帧8字节十六进制指令，输出8字节应答帧。
 *
 *  支持的功能码：
 *    0x01 读寄存器  → 应答DATA=寄存器值
 *    0x02 写寄存器  → 成功应答0xEE, 失败应答0xFF
 *    0x10 LED控制   → 应答DATA=当前LED状态
 *    0x12 心跳      → 应答DATA=运行时间低16位
 *    0x11 传感器上报 → 不应答（设备主动上报）
 ******************************************************************************
 */
#ifndef APP_HEX_CMD_H
#define APP_HEX_CMD_H

#include <stdint.h>

/**
 * @brief  处理一帧十六进制指令并构建应答帧
 * @param  in_frame   输入帧（8字节，已通过CRC校验）
 * @param  out_frame  输出应答帧（8字节）
 * @retval 1=需要发送应答帧, 0=不需要应答（广播帧或上报帧）
 */
uint8_t APP_HexCmd_Process(const uint8_t *in_frame, uint8_t *out_frame);

/**
 * @brief  构建传感器主动上报帧
 * @param  out_frame  输出帧（8字节）
 * @param  reg        数据类型寄存器地址（REG_TEMPERATURE等）
 * @param  data       数据值
 * @retval 帧长度(8)
 */
uint8_t APP_HexCmd_BuildReport(uint8_t *out_frame, uint16_t reg, uint16_t data);

#endif /* APP_HEX_CMD_H */
