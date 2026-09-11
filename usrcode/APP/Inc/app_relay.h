/**
 ******************************************************************************
 * @file    app_relay.h
 * @brief   继电器应用层头文件 — 统一控制入口 + 状态快照
 *
 *  功能：
 *    - 提供 APP_Relay_Control() 统一控制接口（MQTT下行命令、过流保护、
 *      按键等任何模块都通过它操作继电器）
 *    - 每次控制后自动更新统一快照（app_sensor），MQTT 上报直接读快照
 *
 *  无独立任务：继电器是被控对象，不需要周期采集；
 *  状态通过 APP_SENSOR_UpdateRelay() 进入统一快照。
 ******************************************************************************
 */
#ifndef APP_RELAY_H
#define APP_RELAY_H

#include <stdint.h>

/**
 * @brief  继电器控制（统一入口）
 * @param  on  1=吸合(ON，负载通电)，0=断开(OFF，负载断电)
 * @note   控制后自动更新统一快照；空操作（状态未变化）不重复写快照
 */
void APP_Relay_Control(uint8_t on);

/**
 * @brief  获取继电器当前状态
 * @retval 1=吸合(ON)，0=断开(OFF)
 */
uint8_t APP_Relay_GetState(void);

#endif /* APP_RELAY_H */
