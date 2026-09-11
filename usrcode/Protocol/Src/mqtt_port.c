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

    got = BSP_ESP8266_TCPRead((uint8_t *)buf, (uint32_t)len, (uint32_t)timeout_ms);

    if (got == 0U) {
        /* 无数据：判断是超时还是断开 */
        if (BSP_ESP8266_TCPIsAlive() != 0U) {
            return 0;   /* 连接正常，只是暂时无数据 */
        }
        return -1;      /* 连接已断开 */
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
    /* ESP8266 单连接模式下可发 AT+CIPCLOSE，此处简化为标记断开，
     * 重连时 BSP_ESP8266_TCPConnect 会重新 CIPSTART */
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
