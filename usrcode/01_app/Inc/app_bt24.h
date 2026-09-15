/**
 ******************************************************************************
 * @file    app_bt24.h
 * @brief   DX-BT24 蓝牙透传应用层头文件 — 传感器遥测上报 + 小程序下行命令
 *
 *  功能：
 *    - 周期上报诊断快照（温度/湿度/振动/电流/电压/功率/继电器/告警/故障）
 *      JSON 格式，一行一帧（\r\n 结尾），经 BLE 透传通道发给微信小程序
 *    - 解析小程序下行 JSON 命令：QUERY / RELAY / LED / PING / BEEP
 *    - 通过 AT+NOTI1 使模块在手机连接时上报 OK+CONN<mac>，据此判断
 *      是否处于透传模式，仅在连接后主动上报，避免 AT 模式下数据被
 *      模块当作命令解析
 *
 *  协议（一行一帧，UTF-8 无 BOM）：
 *    上行：{"t":25.3,"h":58.0,"v":1.02,"i":0.452,"u":12.100,"p":5.4,
 *           "relay":0,"alarm":0,"diag":0,"mask":0}
 *    命令：{"cmd":"QUERY"} / {"cmd":"RELAY","val":1} /
 *          {"cmd":"LED","mask":5} / {"cmd":"LED","index":1,"state":1} /
 *          {"cmd":"PING"} / {"cmd":"BEEP","ms":200}
 *    应答：{"ok":1,"cmd":"RELAY","val":1} / {"ok":0,"err":"..."}
 ******************************************************************************
 */
#ifndef APP_BT24_H
#define APP_BT24_H

#include "module_cfg.h"

#if APP_BT24_ENABLE

/**
 * @brief  初始化蓝牙链路（BSP 驱动 + 缓冲区），调度器启动前调用
 * @note   APP_Init -> APP_BT24_Init；AT+NOTI1 配置在任务内延时后发送，
 *         避免模块上电未就绪时丢指令
 */
void APP_BT24_Init(void);

/**
 * @brief  BT24 透传任务入口（由 app_tasks.c 创建）
 * @param  argument 未使用
 * @note   FreeRTOS 任务，周期轮询接收 + 3s 周期上报（连接后）
 */
void APP_BT24_Task(void *argument);

#endif /* APP_BT24_ENABLE */
#endif /* APP_BT24_H */
