/**
 ******************************************************************************
 * @file    mqtt_port.c
 * @brief   Paho MQTT 平台移植层实现 —— Timer(基于HAL tick) + Network(对接ESP8266)
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_MQTT_ENABLE

#include "mqtt_port.h"
#include "bsp_esp8266.h"
#include "bsp_uart.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* 模块打印：受 PROTOCOL_MQTT_UART1_PRINTF_ENABLE 开关控制，关闭后编译为空 */
#if PROTOCOL_MQTT_UART1_PRINTF_ENABLE && BSP_UART1_ENABLE
#define MqttNet_Printf(fmt, ...)  BSP_UART1_Printf(fmt, ##__VA_ARGS__)
#else
#define MqttNet_Printf(fmt, ...)  ((void)0)
#endif

/* ==========================================================================
 *  Timer 实现（基于 HAL_GetTick，单位毫秒）
 * ========================================================================== */

void TimerInit(Timer *t)
{
    t->end_tick = 0U;
}

char TimerIsExpired(Timer *t)
{
    return (HAL_GetTick() >= t->end_tick) ? 1 : 0;
}

void TimerCountdownMS(Timer *t, unsigned int ms)
{
    t->end_tick = HAL_GetTick() + (uint32_t)ms;
}

void TimerCountdown(Timer *t, unsigned int sec)
{
    TimerCountdownMS(t, sec * 1000U);
}

int TimerLeftMS(Timer *t)
{
    uint32_t now = HAL_GetTick();
    if (now >= t->end_tick) {
        return 0;
    }
    return (int)(t->end_tick - now);
}

/* ==========================================================================
 *  Network 实现（对接 ESP8266 AT 指令）
 * ========================================================================== */

/**
 * @brief  MQTT 网络读：阻塞读取，支持部分返回
 * @param  n          Network对象（未使用，ESP8266全局单连接）
 * @param  buf        接收缓冲区
 * @param  len        期望字节数
 * @param  timeout_ms 超时（毫秒）
 * @retval >0 实际读到的字节数（可能小于len）；0 超时无数据；-1 连接断开
 */
static int esp8266_mqttread(Network *n, unsigned char *buf, int len, int timeout_ms)
{
    uint32_t got;

    (void)n;

    if ((buf == NULL) || (len <= 0)) {
        return -1;
    }

    /* 原子读：整包就绪才消费，超时不消费（防止 Paho 半个包被消费导致流反同步） */
    got = BSP_ESP8266_TCPReadFull((uint8_t *)buf, (uint32_t)len, (uint32_t)timeout_ms);

    if (got == 0U) {
        /* 无数据：只查断开标志（纯内存读取，不发 AT 命令）。
           s_tcp_closed 由 BSP_ESP8266_TCPRead 内部检测到 "CLOSED" 时置位，
           不在 MQTT 二进制接收模式下发 AT+CIPSTATUS，避免干扰数据接收和刷屏。 */
        if (BSP_ESP8266_IsTCPClosed() != 0U) {
            return -1;      /* 已检测到断开 */
        }
        return 0;           /* 连接正常，只是暂时无数据 */
    }

    return (int)got;
}

/**
 * @brief  MQTT 网络写：通过 ESP8266 TCP 发送数据
 * @retval 成功返回 len；失败返回 -1
 */
static int esp8266_mqttwrite(Network *n, unsigned char *buf, int len, int timeout_ms)
{
    (void)n;
    (void)timeout_ms;   /* BSP_ESP8266_TCPSend 内部已有超时机制 */

    if ((buf == NULL) || (len <= 0)) {
        return -1;
    }

    if (BSP_ESP8266_TCPSend((const uint8_t *)buf, (uint32_t)len) != ESP8266_OK) {
        MqttNet_Printf("[MQTT-NET] mqttwrite FAILED len=%d (TCP send error, connection may be dead)\r\n", len);
        return -1;
    }

    return len;
}

/**
 * @brief  关闭网络连接（Paho同步客户端未直接调用，保留供应用层使用）
 */
static void esp8266_disconnect(Network *n)
{
    (void)n;
    /* 实际关闭 TCP 连接，设置断开标志，重连时 mqtt_full_connect 会重新建连 */
    (void)BSP_ESP8266_TCPClose();
}

void NetworkInit(Network *n)
{
    n->mqttread   = esp8266_mqttread;
    n->mqttwrite  = esp8266_mqttwrite;
    n->disconnect = esp8266_disconnect;
}

/**
 * @brief  建立 TCP 连接到 MQTT Broker，并切换到 MQTT 二进制接收模式
 * @param  n    Network对象
 * @param  host 服务器地址（支持域名，ESP8266 AT+CIPSTART 内部解析）
 * @param  port 端口号
 * @retval 0 成功；-1 失败
 */
int NetworkConnect(Network *n, char *host, int port)
{
    (void)n;

    if ((host == NULL) || (port <= 0)) {
        return -1;
    }

    if (BSP_ESP8266_TCPConnect(host, (uint16_t)port) != ESP8266_OK) {
        return -1;
    }

    /* TCP 连接成功后，立即切换到 MQTT 二进制接收模式（不追加\n） */
    BSP_ESP8266_EnterMQTTMode();

    return 0;
}

#endif /* APP_MQTT_ENABLE */
