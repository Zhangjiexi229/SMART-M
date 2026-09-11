/**
 ******************************************************************************
 * @file    mqtt_port.h
 * @brief   Paho MQTT 平台移植层 —— Network + Timer，对接 ESP8266 AT 指令
 *
 *  本文件必须在包含 MQTTClient.h 之前定义 Network 和 Timer，
 *  因为 MQTTClient 结构体内部引用了这两个类型。
 *
 *  Timer 实现：基于 HAL_GetTick()（SysTick），FreeRTOS 无关，
 *             调度器启动前后均可使用。
 *
 *  Network 实现：对接 bsp_esp8266 的 TCP 连接/发送/MQTT专用读取接口。
 *
 *  4G 模块适配：再写一份 mqtt_port_4g.c，实现同样的 Network 接口即可，
 *             MQTT 应用层代码无需修改。
 ******************************************************************************
 */
#ifndef MQTT_PORT_H
#define MQTT_PORT_H

#include <stdint.h>

/* ==========================================================================
 *  Timer —— 基于 HAL_GetTick()
 * ========================================================================== */
typedef struct Timer {
    uint32_t end_tick;   /*!< 到期时刻（HAL_GetTick 绝对值） */
} Timer;

void TimerInit(Timer *t);
char TimerIsExpired(Timer *t);
void TimerCountdownMS(Timer *t, unsigned int ms);
void TimerCountdown(Timer *t, unsigned int sec);
int  TimerLeftMS(Timer *t);

/* ==========================================================================
 *  Network —— 对接 ESP8266 AT 指令 TCP
 * ========================================================================== */
typedef struct Network {
    int  (*mqttread)(struct Network *, unsigned char *, int, int);
    int  (*mqttwrite)(struct Network *, unsigned char *, int, int);
    void (*disconnect)(struct Network *);   /*!< 可选，Paho同步客户端未直接调用 */
} Network;

void NetworkInit(Network *n);
int  NetworkConnect(Network *n, char *host, int port);

/* 包含 Paho 客户端头文件（此时 Network 和 Timer 已定义）
 *
 * 注意：Paho 的 enum returnCode 定义了 SUCCESS/FAILURE/BUFFER_OVERFLOW，
 * 与 STM32 HAL 库 stm32f4xx.h 中的 HAL_StatusTypeDef 枚举（SUCCESS=0U）冲突。
 * 通过宏重命名 paho 的枚举值，包含后取消宏，避免全局命名空间冲突。
 * MQTTClient.c 单独编译时不包含 HAL 头文件，因此不受影响；
 * 调用方按数值比较（如 rc != 0），重命名不影响二进制兼容。
 */
#define SUCCESS          MQTT_SUCCESS
#define FAILURE          MQTT_FAILURE
#define BUFFER_OVERFLOW  MQTT_BUFFER_OVERFLOW
#include "MQTTClient.h"
#undef SUCCESS
#undef FAILURE
#undef BUFFER_OVERFLOW

#endif /* MQTT_PORT_H */
