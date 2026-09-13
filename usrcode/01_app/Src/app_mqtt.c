/**
 ******************************************************************************
 * @file    app_mqtt.c
 * @brief   MQTT 上云任务 — ESP8266 + Paho MQTT 连接华为云 IoTDA
 *          周期上报温湿度等属性 + 接收云平台命令控制 LED/继电器 + 回复命令响应
 *
 *  本文件由 v2.2a 工程 app_mqtt.c（新版机制）与 SMART-M 原 app_mqtt.c（毕设扩展）
 *  融合而成：
 *    1) v2.2a 新机制：线程安全外部发布队列（APP_MQTT_Publish/PublishTopic）、
 *       离线缓存（16×256B，断网退避期间采集缓存、重连后最新+缓存重传）、
 *       指数退避重连（1s→30s 上限）、连续失败 5 次复位 ESP8266、
 *       10s TCP 存活探测 + 强制 TCP 重建、消息回调只拷贝（不阻塞 MQTTYield）、
 *       命令响应队列化、LED_Control 单灯 + 位掩码双模式
 *    2) SMART-M 扩展：RELAY_Control 继电器命令、SHT30/QMI8658/INA226/继电器/
 *       告警/诊断扩展属性上报（统一快照 app_sensor）、app_offline 断网快照补传
 *
 *  华为云主题：
 *    上报属性(发布): $oc/devices/{device_id}/sys/properties/report
 *    接收命令(订阅): $oc/devices/{device_id}/sys/commands/#
 *    命令响应(发布): $oc/devices/{device_id}/sys/commands/response/request_id={rid}
 *    设备消息上行(发布): $oc/devices/{device_id}/sys/messages/up
 *    设备消息下行(订阅): $oc/devices/{device_id}/sys/messages/down
 *
 *  华为云物模型 JSON：
 *    上报: {"services":[{"service_id":"Sensor","properties":{
 *            "temperature":25.5,"humidity":60.0,           ← SHT30（或DHT11）
 *            "sht_temperature":26.3,"sht_humidity":58.0,   ← SHT30 独立属性
 *            "pitch":1.2,"roll":0.5,"vibration":0.12,"ax":12,"ay":3,"az":1000,
 *            "gx":5,"gy":-2,"gz":8,                        ← QMI8658
 *            "voltage":24.000,"current":1.200,"power":28.800, ← INA226（V/A/W）
 *            "relay_status":1,                             ← 继电器
 *            "alarm_status":0,"diag_status":0,"diag_mask":0}}]} ← 告警/诊断
 *    命令: {"request_id":"xxx","service_id":"Sensor","command_name":"LED_Control","paras":{"led":1}}
 *          {"request_id":"xxx","service_id":"Sensor","command_name":"LED_Control","paras":{"led_index":1,"state":1}}
 *          {"request_id":"xxx","service_id":"Sensor","command_name":"RELAY_Control","paras":{"relay":1}}
 *    响应: {"result_code":0,"response_name":"COMMAND_RESPONSE","paras":{"result":"success"}}
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_MQTT_ENABLE && APP_TASKS_ENABLE && BSP_ESP8266_ENABLE

#include "app_mqtt.h"
#include "mqtt_port.h"
#include "bsp_esp8266.h"
#if APP_WATCHDOG_ENABLE
#include "app_watchdog.h"
#endif
#include "bsp_uart.h"
#if SVC_AI_ENABLE && PLAT_AI_ENABLE && PLAT_SVCMGR_ENABLE
#include "app_ai_engine.h"   /* AI 云端引擎适配器（messages/down 回复识别） */
#endif
#if BSP_LED_ENABLE
#include "bsp_led.h"
#endif
#include "cmsis_os.h"
#if APP_EEPROM_ENABLE && PLAT_EEPROM_ENABLE
#include "app_eeprom.h"
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 传感器数据发布（可选，依赖对应模块启用） */
#if APP_DHT11_ENABLE && BSP_DHT11_ENABLE
#include "app_dht11.h"
#endif
#if APP_LIGHT_SENSOR_ENABLE && BSP_LIGHT_SENSOR_ENABLE
#include "app_light_sensor.h"
#endif
#if APP_SENSOR_ENABLE
#include "app_sensor.h"
#endif
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
#include "app_relay.h"
#endif
#if APP_DIAG_ENABLE
#include "app_diag.h"
#endif
#if APP_ALARM_ENABLE
#include "app_alarm.h"
#endif
#if APP_FAULT_ENABLE
#include "app_fault.h"
#endif
#if APP_OFFLINE_ENABLE
#include "app_offline.h"
#endif

/* 下行指令处理：文本协议 + 十六进制帧 */
#if PROTOCOL_ENABLE
#include "protocol.h"
#include "protocol_hex.h"
#if BSP_LED_ENABLE
#include "app_cmd.h"
#endif
#include "app_hex_cmd.h"
#endif /* PROTOCOL_ENABLE */

/* 模块打印：受 APP_MQTT_UART1_PRINTF_ENABLE 开关控制，关闭后编译为空
 * 注意：独立于 PROTOCOL_ENABLE（MQTT 链路打印与字符协议层无关） */
#if APP_MQTT_UART1_PRINTF_ENABLE && BSP_UART1_ENABLE
#define MQTT_Printf(fmt, ...)  BSP_UART1_Printf(fmt, ##__VA_ARGS__)
#else
#define MQTT_Printf(fmt, ...)  ((void)0)
#endif

/* BSP层长等待回调：在 esp8266_wait_response 中每500ms调用一次，
 * 防止 JoinAP(10s)/TCPConnect(8s) 等长阻塞导致 watchdog stall + IWDG复位 */
#if APP_WATCHDOG_ENABLE
void BSP_ESP8266_WaitHook(void)
{
    Watchdog_Kick(WDT_TASK_MQTT);
}
#endif

/* ==========================================================================
 *  内部资源
 * ========================================================================== */

/* Paho MQTT 客户端实例（静态分配，避免栈溢出） */
static MQTTClient      s_mqtt_client;
static Network         s_mqtt_network;
static unsigned char   s_mqtt_sendbuf[512];   /*!< 发送缓冲，按最大报文调整 */
static unsigned char   s_mqtt_readbuf[512];   /*!< 接收缓冲 */

/* 连接状态（供外部查询） */
static volatile uint8_t s_mqtt_status = 0U;  /* bit0=WiFi, bit1=TCP, bit2=MQTT */

/* 华为云主题缓冲（运行时拼接 device_id，连接成功后由 huawei_build_topics 填充；
 * 声明置于文件前部，供 mqtt_flush_offline_cache / mqtt_flush_ext_publish 等使用） */
static char s_topic_pub[128];    /*!< 上报属性主题 */
static char s_topic_sub[128];    /*!< 订阅命令主题 */
static char s_topic_resp[160];   /*!< 命令响应主题（含request_id） */

/* ===== Offline cache: store failed publishes, retransmit after reconnect ===== */
static char    s_offline_buf[MQTT_OFFLINE_CACHE_MAX][256];
static uint8_t s_offline_len[MQTT_OFFLINE_CACHE_MAX];
static uint8_t s_offline_head = 0U;
static uint8_t s_offline_tail = 0U;
static uint8_t s_offline_count = 0U;

static void offline_cache_push(const char *payload, uint8_t len)
{
    if (len > sizeof(s_offline_buf[0])) {
        len = (uint8_t)sizeof(s_offline_buf[0]);
    }
    memcpy(s_offline_buf[s_offline_head], payload, len);
    s_offline_len[s_offline_head] = len;
    s_offline_head = (uint8_t)((s_offline_head + 1U) % MQTT_OFFLINE_CACHE_MAX);
    if (s_offline_count < MQTT_OFFLINE_CACHE_MAX) {
        s_offline_count++;
    } else {
        s_offline_tail = (uint8_t)((s_offline_tail + 1U) % MQTT_OFFLINE_CACHE_MAX);
    }
}

/* ===== 外部发布队列：任意任务入队（线程安全），MQTT任务在Yield循环中消费发布 =====
 * 入队用 osKernelLock 短临界区保护；消费只在 MQTT 任务上下文，不与 MQTTPublish 并发 */
#define MQTT_EXT_PUB_QUEUE_MAX  8U
#define MQTT_EXT_PUB_LEN_MAX    256U
#define MQTT_EXT_PUB_TOPIC_MAX  24U
static uint8_t  s_ext_pub_buf[MQTT_EXT_PUB_QUEUE_MAX][MQTT_EXT_PUB_LEN_MAX];
static uint16_t s_ext_pub_len[MQTT_EXT_PUB_QUEUE_MAX];
static char     s_ext_pub_topic[MQTT_EXT_PUB_QUEUE_MAX][MQTT_EXT_PUB_TOPIC_MAX];   /* 主题短名：report / messages/up / ... */
static uint8_t  s_ext_pub_head  = 0U;
static uint8_t  s_ext_pub_tail  = 0U;
static uint8_t  s_ext_pub_count = 0U;

/* 主题短名 → 完整主题（连接成功后由 huawei_build_topics 填充） */
static char s_topic_msg_up[128];    /*!< 设备消息上行主题：/devices/{id}/sys/messages/up */
static char s_topic_msg_down[128];  /*!< 设备消息下行主题：/devices/{id}/sys/messages/down */

static uint8_t mqtt_flush_offline_cache(MQTTClient *client)
{
    uint8_t sent = 0U;
    while (s_offline_count > 0U) {
        MQTTMessage msg;
        int rc;
        msg.qos        = (enum QoS)MQTT_QOS;
        msg.retained   = 0;
        msg.dup        = 0;
        msg.id         = 0;
        msg.payload    = s_offline_buf[s_offline_tail];
        msg.payloadlen = s_offline_len[s_offline_tail];
        rc = MQTTPublish(client, s_topic_pub, &msg);
        if (rc != 0) {
            MQTT_Printf("[MQTT] Offline flush failed at %u/%u\r\n",
                             (unsigned)(sent + 1U), (unsigned)s_offline_count);
            break;
        }
        s_offline_tail = (uint8_t)((s_offline_tail + 1U) % MQTT_OFFLINE_CACHE_MAX);
        s_offline_count--;
        sent++;
    }
    if (sent > 0U) {
        MQTT_Printf("[MQTT] Offline cache flushed: %u messages\r\n", (unsigned)sent);
    }
    return sent;
}

/* 连续连接失败计数：用于指数退避和模块复位自愈 */
static uint32_t s_connect_fail_count = 0U;
/* MQTT CONNECT 失败后强制下次重建 TCP（应对半开连接：ESP8266 显示已连但 Broker 已断） */
static uint8_t s_force_tcp_rebuild = 0U;
#define MQTT_MAX_FAIL_BEFORE_RESET  5U    /* 连续失败 5 次后复位 ESP8266 */
#define MQTT_RECONNECT_BASE_MS      1000U /* 退避基数 1 秒 */
#define MQTT_RECONNECT_MAX_MS       8000U/* 退避上限 8 秒（对齐v2.2a加快恢复，保留退避防风暴） */

/* 待回复缓冲：在 mqtt_message_handler 回调中填充，在任务主循环中发布。
 * 不能在回调内直接调用 MQTTPublish——回调运行在 MQTTYield 的读取上下文中，
 * 重入会破坏 paho 的接收状态机。 */
#define MQTT_REPLY_BUF_SIZE  128U
static uint8_t           s_reply_payload[MQTT_REPLY_BUF_SIZE];
static uint16_t          s_reply_len;
static volatile uint8_t  s_reply_pending;
static char              s_reply_request_id[64];  /*!< 华为云命令的request_id，响应时需原样返回 */

/* 原始命令缓冲：回调中只拷贝，主循环中处理（避免回调内阻塞导致 MQTTYield 失败） */
#define MQTT_CMD_TOPIC_SIZE   128U
#define MQTT_CMD_PAYLOAD_SIZE 256U
static char              s_cmd_topic[MQTT_CMD_TOPIC_SIZE];
static char              s_cmd_payload[MQTT_CMD_PAYLOAD_SIZE];
static volatile uint8_t  s_cmd_pending;

/* 毕设扩展属性 JSON 片段（SHT30/QMI8658/INA226/继电器/告警/诊断），
 * 每次上报前由统一快照生成，带前导逗号，长度上限按最大上报数估算 */
#define MQTT_EXTRA_BUF_SIZE  256U
static char                s_report_extra[MQTT_EXTRA_BUF_SIZE];

/* ==========================================================================
 *  华为云主题拼接
 * ========================================================================== */

/**
 * @brief  拼接华为云固定格式主题（含device_id）
 * @note   在连接成功后调用一次即可
 */
static void huawei_build_topics(void)
{
    snprintf(s_topic_pub, sizeof(s_topic_pub),
             "$oc/devices/%s/sys/properties/report", HUAWEI_DEVICE_ID);
    snprintf(s_topic_sub, sizeof(s_topic_sub),
             "$oc/devices/%s/sys/commands/#", HUAWEI_DEVICE_ID);
    snprintf(s_topic_msg_up, sizeof(s_topic_msg_up),
             "$oc/devices/%s/sys/messages/up", HUAWEI_DEVICE_ID);
    snprintf(s_topic_msg_down, sizeof(s_topic_msg_down),
             "$oc/devices/%s/sys/messages/down", HUAWEI_DEVICE_ID);
}

/**
 * @brief  从MQTT主题中提取 request_id
 * @param  topic  主题字符串（格式: $oc/devices/xxx/sys/commands/request_id=yyy）
 * @param  out    输出缓冲区
 * @param  out_size 输出缓冲区大小
 * @retval 1=成功提取, 0=未找到
 */
static uint8_t huawei_extract_request_id(const char *topic, char *out, uint16_t out_size)
{
    const char *p = strstr(topic, "request_id=");
    if (p == NULL) {
        return 0U;
    }
    p += strlen("request_id=");
    strncpy(out, p, out_size - 1U);
    out[out_size - 1U] = '\0';
    return 1U;
}

/**
 * @brief  从JSON payload中提取字符串字段值
 * @param  json   JSON字符串
 * @param  key    字段名（如 "command_name"）
 * @param  out    输出值缓冲区
 * @param  out_size 输出缓冲区大小
 * @retval 1=成功, 0=未找到
 */
static uint8_t json_get_string(const char *json, const char *key, char *out, uint16_t out_size)
{
    char search[64];
    const char *p;
    const char *start;
    const char *end;
    uint16_t len;

    snprintf(search, sizeof(search), "\"%s\":\"", key);
    p = strstr(json, search);
    if (p == NULL) {
        return 0U;
    }
    start = p + strlen(search);
    end = strchr(start, '"');
    if (end == NULL) {
        return 0U;
    }
    len = (uint16_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1U;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1U;
}

/**
 * @brief  从JSON payload中提取整数值（支持嵌套对象，如 paras.led）
 * @param  json  JSON字符串
 * @param  key   字段名（如 "led"）
 * @param  value 输出整数值
 * @retval 1=成功, 0=未找到
 */
static uint8_t json_get_int(const char *json, const char *key, int *value)
{
    char search[32];
    const char *p;
    const char *start;

    snprintf(search, sizeof(search), "\"%s\":", key);
    p = strstr(json, search);
    if (p == NULL) {
        return 0U;
    }
    start = p + strlen(search);
    /* 跳过空格 */
    while (*start == ' ' || *start == '\t') start++;
    *value = atoi(start);
    return 1U;
}

/* ==========================================================================
 *  云平台下行命令处理（华为云物模型命令）
 * ========================================================================== */

/**
 * @brief  执行LED控制命令（位掩码模式）
 * @param  led_mask  LED位掩码（bit0=LED1, bit1=LED2, bit2=LED3, bit3=LED4）
 */
static void huawei_exec_led(int led_mask)
{
#if BSP_LED_ENABLE
    if (led_mask & 0x01) BSP_LED_On(BSP_LED1);  else BSP_LED_Off(BSP_LED1);
    if (led_mask & 0x02) BSP_LED_On(BSP_LED2);  else BSP_LED_Off(BSP_LED2);
    if (led_mask & 0x04) BSP_LED_On(BSP_LED3);  else BSP_LED_Off(BSP_LED3);
    if (led_mask & 0x08) BSP_LED_On(BSP_LED4);  else BSP_LED_Off(BSP_LED4);
#else
    (void)led_mask;
#endif
}

/**
 * @brief  按灯号单独控制LED（不影响其他灯状态）
 * @param  led_index  灯号 1~4（非法值忽略）
 * @param  state      0=灭, 1=亮
 * @note   执行后同步保存到 EEPROM（若启用），掉电重启自动恢复
 */
static void huawei_exec_led_single(int led_index, int state)
{
#if BSP_LED_ENABLE
    BSP_LED_t led;

    switch (led_index) {
        case 1: led = BSP_LED1; break;
        case 2: led = BSP_LED2; break;
        case 3: led = BSP_LED3; break;
        case 4: led = BSP_LED4; break;
        default:
            MQTT_Printf("[MQTT-CMD] LED_Control invalid index=%d\r\n", led_index);
            return;
    }
    if (state) {
        BSP_LED_On(led);
    } else {
        BSP_LED_Off(led);
    }
#if APP_EEPROM_ENABLE && PLAT_EEPROM_ENABLE
    APP_EEPROM_SetLEDState((uint8_t)(led_index - 1), (uint8_t)state);
#endif
    MQTT_Printf("[MQTT-CMD] LED%d %s\r\n", led_index, state ? "ON" : "OFF");
#else
    (void)led_index;
    (void)state;
#endif
}

/**
 * @brief  把待回复数据存入缓冲（在回调中调用，不直接发布）
 */
static void mqtt_queue_reply(const uint8_t *data, uint16_t len, const char *request_id)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }
    if (len > MQTT_REPLY_BUF_SIZE) {
        len = MQTT_REPLY_BUF_SIZE;
    }
    memcpy(s_reply_payload, data, len);
    s_reply_len = len;
    if (request_id != NULL) {
        strncpy(s_reply_request_id, request_id, sizeof(s_reply_request_id) - 1U);
        s_reply_request_id[sizeof(s_reply_request_id) - 1U] = '\0';
    }
    s_reply_pending = 1U;
}

/**
 * @brief  在任务主循环中发布命令响应（避免回调内重入 MQTTPublish）
 * @note   应在 MQTTYield 返回后、下一次发布前调用
 */
static void mqtt_flush_reply(MQTTClient *client)
{
    MQTTMessage msg;

    if (s_reply_pending == 0U) {
        return;
    }
    s_reply_pending = 0U;

    /* 拼接响应主题：$oc/devices/{id}/sys/commands/response/request_id={rid} */
    snprintf(s_topic_resp, sizeof(s_topic_resp),
             "$oc/devices/%s/sys/commands/response/request_id=%s",
             HUAWEI_DEVICE_ID, s_reply_request_id);

    msg.qos        = (enum QoS)MQTT_QOS;
    msg.retained   = 0;
    msg.dup        = 0;
    msg.id         = 0;
    msg.payload    = s_reply_payload;
    msg.payloadlen = s_reply_len;

    if (MQTTPublish(client, s_topic_resp, &msg) != 0) {
        MQTT_Printf("[MQTT] Command response publish failed\r\n");
    } else {
        MQTT_Printf("[MQTT] Command response sent (request_id=%s)\r\n", s_reply_request_id);
    }
}

/**
 * @brief  在任务主循环中发布外部发布队列中的数据（MQTT任务上下文，无并发）
 * @note   发布失败时转入离线缓存（断线重连后自动重传），避免重复入队
 */
static void mqtt_flush_ext_publish(MQTTClient *client)
{
    MQTTMessage msg;
    const char *pub_topic;

    while (s_ext_pub_count > 0U) {
        /* 主题短名解析：""=属性上报, "messages/up"=设备消息上行 */
        if (s_ext_pub_topic[s_ext_pub_tail][0] == '\0') {
            pub_topic = s_topic_pub;
        } else if (strcmp(s_ext_pub_topic[s_ext_pub_tail], "messages/up") == 0) {
            pub_topic = s_topic_msg_up;
        } else {
            pub_topic = s_topic_pub;   /* 未知短名回落属性上报 */
        }

        msg.qos        = (enum QoS)MQTT_QOS;
        msg.retained   = 0;
        msg.dup        = 0;
        msg.id         = 0;
        msg.payload    = s_ext_pub_buf[s_ext_pub_tail];
        msg.payloadlen = s_ext_pub_len[s_ext_pub_tail];

        if (MQTTPublish(client, pub_topic, &msg) != 0) {
            /* 发布失败：转入离线缓存（断线重连后自动重传），弹出本队列避免重复 */
            offline_cache_push((const char *)s_ext_pub_buf[s_ext_pub_tail], (uint8_t)s_ext_pub_len[s_ext_pub_tail]);
            s_ext_pub_tail = (uint8_t)((s_ext_pub_tail + 1U) % MQTT_EXT_PUB_QUEUE_MAX);
            s_ext_pub_count--;
            return;
        }
        MQTT_Printf("[MQTT] Ext publish OK (%u bytes)\r\n", (unsigned)s_ext_pub_len[s_ext_pub_tail]);
        s_ext_pub_tail = (uint8_t)((s_ext_pub_tail + 1U) % MQTT_EXT_PUB_QUEUE_MAX);
        s_ext_pub_count--;
    }
}

/**
 * @brief  Paho 消息回调（运行在 MQTTYield 上下文中）
 * @note   只做数据拷贝，所有处理（JSON解析/LED控制/EEPROM/响应构建）
 *         移到主循环 mqtt_process_command() 中执行，避免回调阻塞导致 Yield 失败
 */
static void mqtt_message_handler(MessageData *md)
{
    int topic_len;
    int payload_len;

    if (s_cmd_pending != 0U) {
        return;  /* 上一条命令尚未处理，丢弃（避免覆盖） */
    }

    /* 拷贝 topic（Paho lenstring 不以'\0'结尾） */
    topic_len = (int)md->topicName->lenstring.len;
    if (topic_len >= (int)MQTT_CMD_TOPIC_SIZE) {
        topic_len = (int)MQTT_CMD_TOPIC_SIZE - 1;
    }
    memcpy(s_cmd_topic, md->topicName->lenstring.data, (size_t)topic_len);
    s_cmd_topic[topic_len] = '\0';

    /* 拷贝 payload */
    payload_len = (int)md->message->payloadlen;
    if (payload_len >= (int)MQTT_CMD_PAYLOAD_SIZE) {
        payload_len = (int)MQTT_CMD_PAYLOAD_SIZE - 1;
    }
    memcpy(s_cmd_payload, md->message->payload, (size_t)payload_len);
    s_cmd_payload[payload_len] = '\0';

    s_cmd_pending = 1U;
}

/**
 * @brief  在主循环中处理命令（MQTTYield 返回后调用，不阻塞网络栈）
 */
static void mqtt_process_command(void)
{
    char command_name[32];
    char request_id[64];
    int  led_value = 0;
    char response[128];
    uint16_t resp_len;

    if (s_cmd_pending == 0U) {
        return;
    }
    s_cmd_pending = 0U;

    /* AI 回复识别：设备消息下行主题（云端大模型回复） */
#if SVC_AI_ENABLE && PLAT_AI_ENABLE && PLAT_SVCMGR_ENABLE
    if (strstr(s_cmd_topic, "sys/messages/down") != NULL) {
        MQTT_Printf("[MQTT-AI] Reply: %s\r\n", s_cmd_payload);
        (void)app_ai_engine_on_down(s_cmd_payload);
        return;
    }
#endif

    /* 打印原始消息（调试用，移到此处不阻塞 MQTTYield） */
    MQTT_Printf("[MQTT] RECV topic=%s len=%u\r\n",
                     s_cmd_topic, (unsigned)strlen(s_cmd_payload));
    MQTT_Printf("[MQTT] payload: %s\r\n", s_cmd_payload);

    /* 从主题提取 request_id */
    if (huawei_extract_request_id(s_cmd_topic, request_id, sizeof(request_id)) == 0U) {
        MQTT_Printf("[MQTT] request_id not found in topic, ignored\r\n");
        return;
    }

    /* 从 JSON 提取 command_name */
    if (json_get_string(s_cmd_payload, "command_name", command_name, sizeof(command_name)) == 0U) {
        MQTT_Printf("[MQTT] command_name not found, ignored\r\n");
        return;
    }

    MQTT_Printf("[MQTT-CMD] Huawei command: %s, request_id=%s\r\n", command_name, request_id);

    /* 处理 LED_Control 命令 */
    if (strcmp(command_name, "LED_Control") == 0) {
        int led_index = 0;
        int led_state = 0;

        /* 模式1（新）：单灯独立控制 — paras: {"led_index":1~4,"state":0/1} */
        if ((json_get_int(s_cmd_payload, "led_index", &led_index) != 0U) &&
            (json_get_int(s_cmd_payload, "state", &led_state) != 0U)) {
            huawei_exec_led_single(led_index, led_state);
            resp_len = (uint16_t)snprintf(response, sizeof(response),
                "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"success\",\"led_index\":%d,\"state\":%d}}",
                led_index, led_state);
            mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
        }
        /* 模式2（兼容旧版）：位掩码 — paras: {"led":bit0~3=LED1~4} */
        else if (json_get_int(s_cmd_payload, "led", &led_value) != 0U) {
            MQTT_Printf("[MQTT-CMD] LED_Control led=%d\r\n", led_value);
            huawei_exec_led(led_value);
            resp_len = (uint16_t)snprintf(response, sizeof(response),
                "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"success\",\"led\":%d}}",
                led_value);
            mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
        } else {
            resp_len = (uint16_t)snprintf(response, sizeof(response),
                "{\"result_code\":1,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"missing parameter\"}}");
            mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
        }
    }
    /* 处理 RELAY_Control 命令（SMART-M 毕设：远程控制电机电源通断） */
    else if (strcmp(command_name, "RELAY_Control") == 0) {
        int relay_value = 0;
        if (json_get_int(s_cmd_payload, "relay", &relay_value) != 0U) {
            MQTT_Printf("[MQTT-CMD] RELAY_Control relay=%d\r\n", relay_value);
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
            APP_Relay_Control((uint8_t)((relay_value != 0) ? 1U : 0U));
#endif
            /* 构建成功响应 */
            resp_len = (uint16_t)snprintf(response, sizeof(response),
                "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"success\",\"relay\":%d}}",
                (relay_value != 0) ? 1 : 0);
            mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
        } else {
            /* 参数缺失，返回失败响应 */
            resp_len = (uint16_t)snprintf(response, sizeof(response),
                "{\"result_code\":1,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"missing parameter 'relay'\"}}");
            mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
        }
    }
    else {
        MQTT_Printf("[MQTT-CMD] Unknown command: %s\r\n", command_name);
        resp_len = (uint16_t)snprintf(response, sizeof(response),
            "{\"result_code\":2,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"unknown command\"}}");
        mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
    }
}

/* ==========================================================================
 *  公共 API
 * ========================================================================== */

uint8_t APP_MQTT_GetStatus(void)
{
    return s_mqtt_status;
}

uint8_t APP_MQTT_PublishTopic(const char *topic, const uint8_t *payload, uint16_t len)
{
    int32_t lock;
    uint8_t idx;

    if ((payload == NULL) || (len == 0U)) return 1U;
    if (len > MQTT_EXT_PUB_LEN_MAX) {
        len = MQTT_EXT_PUB_LEN_MAX;
    }

    lock = osKernelLock();   /* 短临界区保护环形队列（仅内存拷贝） */
    if (s_ext_pub_count >= MQTT_EXT_PUB_QUEUE_MAX) {
        if (lock >= 0) osKernelUnlock();
        return 3U;           /* 队列满：调用方自行缓冲 */
    }
    idx = s_ext_pub_head;
    memcpy(s_ext_pub_buf[idx], payload, len);
    s_ext_pub_len[idx] = len;
    if (topic != NULL) {
        strncpy(s_ext_pub_topic[idx], topic, MQTT_EXT_PUB_TOPIC_MAX - 1U);
        s_ext_pub_topic[idx][MQTT_EXT_PUB_TOPIC_MAX - 1U] = '\0';
    } else {
        s_ext_pub_topic[idx][0] = '\0';   /* 空=默认属性上报主题 */
    }
    s_ext_pub_head = (uint8_t)((s_ext_pub_head + 1U) % MQTT_EXT_PUB_QUEUE_MAX);
    s_ext_pub_count++;
    if (lock >= 0) osKernelUnlock();
    return 0U;
}

uint8_t APP_MQTT_Publish(const uint8_t *payload, uint16_t len)
{
    return APP_MQTT_PublishTopic(NULL, payload, len);
}

/* ==========================================================================
 *  内部辅助函数
 * ========================================================================== */

/**
 * @brief  计算指数退避延迟：1s → 2s → 4s → 8s → 16s → 30s(上限)
 */
static uint32_t mqtt_backoff_delay(void)
{
    /* fail_count=0→1s, 1→2s, 2→4s, 3→8s, 4→16s, >=5→30s */
    uint32_t shift = (s_connect_fail_count < 5U) ? s_connect_fail_count : 5U;
    uint32_t delay = MQTT_RECONNECT_BASE_MS << shift;
    if (delay > MQTT_RECONNECT_MAX_MS) {
        delay = MQTT_RECONNECT_MAX_MS;
    }
    return delay;
}

/**
 * @brief  分段智能连接：AT → WiFi(已连则跳过) → TCP(已连则跳过) → MQTT → 订阅
 * @retval 1=全部成功, 0=失败
 *
 *  重连优化：
 *    - WiFi 未掉线时不重发 JoinAP（省 2~3 秒）
 *    - TCP 未断开时不重新 CIPSTART（省 1~2 秒）
 *    - MQTT 会话每次都重建（Paho 客户端状态无法从外部查询）
 *    - AT 失败时复位模块，避免死循环
 *    - MQTT CONNECT 失败时主动 CIPCLOSE，让下次重连走 TCP 重建
 */
static uint8_t mqtt_full_connect(void)
{
    int rc;
    MQTTPacket_connectData connect_data = MQTTPacket_connectData_initializer;
    char   ip_buf[16];
    int8_t tcp_ok;

    /* ── 1. AT 测试（模块在线？） ── */
    MQTT_Printf("[MQTT] AT test...\r\n");
    if (BSP_ESP8266_TestAT(2000U) != ESP8266_OK) {
        MQTT_Printf("[MQTT] AT test FAILED, reset module...\r\n");
        (void)BSP_ESP8266_Reset();
        osDelay(1500U);
        return 0U;  /* 复位后下次循环再试 */
    }
    MQTT_Printf("[MQTT] AT OK\r\n");

    /* ── 2. WiFi 阶段：必须确认获取到有效 IP 才进入下一阶段 ── */
    /* 复位后首次连接(s_connect_fail_count==0)：强制重设STA模式，避免模块残留旧状态 */
    if ((s_connect_fail_count == 0U) && (BSP_ESP8266_SetMode(ESP8266_MODE_STA) != ESP8266_OK)) {
        MQTT_Printf("[MQTT] Set STA mode FAILED (post-reset), abort connect\r\n");
        return 0U;
    }
    if (BSP_ESP8266_GetIP(ip_buf, sizeof(ip_buf)) == ESP8266_OK) {
        /* 热点断开后 CIFSR 可能仍返回旧 IP（假在线），用 CIPSTATUS 二次确认 WiFi 真正在线 */
        if (BSP_ESP8266_IsWiFiConnected() != 0U) {
            s_mqtt_status |= 0x01U;
            MQTT_Printf("[MQTT] WiFi already connected (IP=%s), skip join\r\n", ip_buf);
        } else {
            MQTT_Printf("[MQTT] Stale IP=%s but WiFi link lost (STATUS:5), rejoin...\r\n", ip_buf);
            if (BSP_ESP8266_SetMode(ESP8266_MODE_STA) != ESP8266_OK) {
                MQTT_Printf("[MQTT] Set STA mode FAILED, abort connect\r\n");
                return 0U;
            }
            if (BSP_ESP8266_JoinAP(MQTT_WIFI_SSID, MQTT_WIFI_PASSWORD) != ESP8266_OK) {
                MQTT_Printf("[MQTT] WiFi rejoin FAILED, abort connect\r\n");
                return 0U;
            }
            osDelay(300U);
            if (BSP_ESP8266_GetIP(ip_buf, sizeof(ip_buf)) != ESP8266_OK) {
                MQTT_Printf("[MQTT] WiFi rejoin OK but IP still invalid, abort connect\r\n");
                return 0U;
            }
            s_mqtt_status |= 0x01U;
            MQTT_Printf("[MQTT] WiFi reconnected (IP=%s)\r\n", ip_buf);
        }
    } else {
        MQTT_Printf("[MQTT] WiFi down (no valid IP), rejoin AP \"%s\" ...\r\n", MQTT_WIFI_SSID);
        if (BSP_ESP8266_SetMode(ESP8266_MODE_STA) != ESP8266_OK) {
            MQTT_Printf("[MQTT] Set STA mode FAILED, abort connect\r\n");
            return 0U;  /* WiFi 阶段失败，不进入 TCP */
        }
        if (BSP_ESP8266_JoinAP(MQTT_WIFI_SSID, MQTT_WIFI_PASSWORD) != ESP8266_OK) {
            MQTT_Printf("[MQTT] WiFi join FAILED. Check SSID/password/2.4GHz, abort connect\r\n");
            return 0U;  /* WiFi 阶段失败，不进入 TCP */
        }
        /* JoinAP 返回 "WIFI GOT IP" 后二次确认 IP 有效，防止 DHCP 假成功 */
        osDelay(300U);
        if (BSP_ESP8266_GetIP(ip_buf, sizeof(ip_buf)) != ESP8266_OK) {
            MQTT_Printf("[MQTT] WiFi join OK but IP still invalid, abort connect\r\n");
            return 0U;  /* WiFi 阶段失败，不进入 TCP */
        }
        s_mqtt_status |= 0x01U;
        MQTT_Printf("[MQTT] WiFi connected (IP=%s)\r\n", ip_buf);
    }

    /* ── 3. TCP 阶段（仅在 WiFi 确认有效后执行） ── */
    tcp_ok = BSP_ESP8266_IsTCPConnected();
    if ((tcp_ok == 1) && (s_force_tcp_rebuild == 0U)) {
        s_mqtt_status |= 0x02U;
        MQTT_Printf("[MQTT] TCP already connected, skip CIPSTART\r\n");
        BSP_ESP8266_ClearClosedFlag();         /* 清除残留的断开标志 */
        NetworkInit(&s_mqtt_network);          /* 仅重置函数指针，不发 AT */
        BSP_ESP8266_EnterMQTTMode();           /* 确保二进制接收模式 */
    } else {
        /* 连接目标：默认域名直连（与 v2.2a 一致）；若定义了 MQTT_BROKER_IP（IPv4 地址），
           跳过 ESP8266 模块 DNS 直接用 IP 建连——本设备专属域名带 IPv6(AAAA) 记录，
           部分 ESP8266 AT 固件解析会超时导致 TCP connect FAILED 循环 */
        const char *tcp_target;
#ifdef MQTT_BROKER_IP
        tcp_target = (MQTT_BROKER_IP[0] != '\0') ? (const char *)MQTT_BROKER_IP
                                                 : (const char *)MQTT_BROKER_HOST;
#else
        tcp_target = (const char *)MQTT_BROKER_HOST;
#endif
        if (s_force_tcp_rebuild != 0U) {
            MQTT_Printf("[MQTT] Force TCP rebuild (previous MQTT CONNECT failed)\r\n");
            (void)BSP_ESP8266_TCPClose();      /* 先关闭可能半开的连接 */
            s_force_tcp_rebuild = 0U;
            osDelay(200U);
        }
        MQTT_Printf("[MQTT] TCP connecting %s:%u ...\r\n",
                         tcp_target, (unsigned)MQTT_BROKER_PORT);
        NetworkInit(&s_mqtt_network);
        if (NetworkConnect(&s_mqtt_network, (char *)tcp_target, (int)MQTT_BROKER_PORT) != 0) {
            MQTT_Printf("[MQTT] TCP connect FAILED. Check broker address/port/firewall\r\n");
            return 0U;
        }
        s_mqtt_status |= 0x02U;
        BSP_ESP8266_ClearClosedFlag();         /* 连接成功，清除断开标志 */
        MQTT_Printf("[MQTT] TCP connected\r\n");
    }

    /* ── 4. MQTT 客户端初始化 ── */
    MQTTClientInit(&s_mqtt_client, &s_mqtt_network,
                   3000U,                          /* 命令超时 3 秒（给 Broker 充足响应时间） */
                   s_mqtt_sendbuf, sizeof(s_mqtt_sendbuf),
                   s_mqtt_readbuf, sizeof(s_mqtt_readbuf));

    /* ── 5. 发送 MQTT CONNECT 报文（华为云密钥鉴权） ── */
    connect_data.MQTTVersion           = 4;   /* MQTT 3.1.1 */
    connect_data.clientID.cstring      = (char *)MQTT_CLIENT_ID;
    connect_data.keepAliveInterval     = MQTT_KEEPALIVE_SEC;
    connect_data.cleansession          = 1;
    connect_data.username.cstring      = (char *)MQTT_USERNAME;
    connect_data.password.cstring      = (char *)MQTT_PASSWORD;

    MQTT_Printf("[MQTT] Sending MQTT CONNECT (client=%s, keepalive=%ds)...\r\n",
                     MQTT_CLIENT_ID, MQTT_KEEPALIVE_SEC);
    rc = MQTTConnect(&s_mqtt_client, &connect_data);
    if (rc != 0) {
        MQTT_Printf("[MQTT] MQTT CONNECT FAILED rc=%d (0=ok, -1=send/timeout/parse)\r\n", rc);
        MQTT_Printf("[MQTT]   possible causes: TCP half-open, broker unreachable, auth error, timeout\r\n");
        (void)BSP_ESP8266_TCPClose();   /* 关闭半死 TCP */
        s_force_tcp_rebuild = 1U;       /* 下次强制重建 TCP，不跳过 CIPSTART */
        return 0U;
    }
    s_force_tcp_rebuild = 0U;  /* 连接成功，清除强制重建标志 */
    s_mqtt_status |= 0x04U;
    MQTT_Printf("[MQTT] MQTT connected (client=%s)\r\n", MQTT_CLIENT_ID);

    /* ── 6. 拼接华为云主题 ── */
    huawei_build_topics();

    /* ── 7. 订阅命令主题 ── */
    rc = MQTTSubscribe(&s_mqtt_client, s_topic_sub, (enum QoS)MQTT_QOS, mqtt_message_handler);
    if (rc != 0) {
        MQTT_Printf("[MQTT] Subscribe FAILED rc=%d, continue without subscription\r\n", rc);
    } else {
        MQTT_Printf("[MQTT] Subscribed to \"%s\" (QoS=%d)\r\n", s_topic_sub, MQTT_QOS);
    }

    /* 订阅设备消息下行主题（AI 回复 / 云端自定义下发） */
    rc = MQTTSubscribe(&s_mqtt_client, s_topic_msg_down, (enum QoS)MQTT_QOS, mqtt_message_handler);
    if (rc != 0) {
        MQTT_Printf("[MQTT] Subscribe msg_down FAILED rc=%d, continue\r\n", rc);
    } else {
        MQTT_Printf("[MQTT] Subscribed to \"%s\" (QoS=%d)\r\n", s_topic_msg_down, MQTT_QOS);
    }

    return 1U;
}

/**
 * @brief  采集传感器数据并组装华为云物模型 JSON（不发布，供断网缓存和正常发布共用）
 * @param  payload  输出缓冲区
 * @param  buf_size 缓冲区大小
 * @retval >0=JSON长度, <=0=失败
 *
 *  断网期间调用此函数采集数据并缓存到离线队列，网络恢复后批量重传，避免数据丢失。
 *  属性来源（SMART-M 统一快照 app_sensor）：
 *    temperature/humidity ← SHT30（DHT11 停用时补充，SHT30 启用时）
 *    sht_temperature/sht_humidity ← SHT30
 *    pitch/roll/vibration/ax/ay/az/gx/gy/gz ← QMI8658 + 诊断振动
 *    voltage/current/power ← INA226（mV/mA/mW 转 V/A/W）
 *    relay_status ← 继电器
 *    alarm_status/diag_status/diag_mask ← 告警 + 故障诊断
 */
static int mqtt_build_payload(char *payload, size_t buf_size)
{
    int len = 0;

    /* 传感器数据缓存（无条件声明避免条件编译组合下的未定义引用；
     * 模块关闭时保持 0，不进入 JSON） */
    uint8_t  has_temp = 0U, has_hum = 0U, has_light = 0U;
    unsigned t_int = 0U, t_dec = 0U;
    unsigned h_int = 0U, h_dec = 0U;
    unsigned light_adc = 0U;

#if APP_DHT11_ENABLE && BSP_DHT11_ENABLE
    {
        DHT11_Snapshot_t dht;
        APP_DHT11_GetSnapshot(&dht);
        if (dht.valid != 0U) {
            has_temp = 1U;
            has_hum  = 1U;
            t_int = dht.temperature_int;
            t_dec = dht.temperature_dec;
            h_int = dht.humidity_int;
            h_dec = dht.humidity_dec;
        }
    }
#endif

#if APP_LIGHT_SENSOR_ENABLE && BSP_LIGHT_SENSOR_ENABLE
    {
        LightSensor_Snapshot_t light;
        APP_LightSensor_GetSnapshot(&light);
        if (light.valid != 0U) {
            has_light  = 1U;
            light_adc  = light.adc_value;
        }
    }
#endif

#if APP_SENSOR_ENABLE
    /* 毕设扩展外设数据（统一快照一次读取：SHT30/QMI8658/INA226/继电器） */
    {
        APP_Sensor_Snapshot_t sn;
        APP_SENSOR_GetSnapshot(&sn);

        /* SHT30 温湿度（若DHT11无效且SHT30有效且温度非负，用SHT30补充 temperature/humidity；
         * 负温场景仅走下方独立属性 sht_temperature，避免 %u 无符号格式把负值变成大数） */
        if ((has_temp == 0U) && (sn.sht_valid != 0U) && (sn.sht_temp_int >= 0)) {
            has_temp = 1U;
            has_hum  = 1U;
            t_int = (unsigned)sn.sht_temp_int;
            t_dec = (unsigned)sn.sht_temp_dec;
            h_int = (unsigned)sn.sht_hum_int;
            h_dec = (unsigned)sn.sht_hum_dec;
        }
        /* SHT30 独立属性（物模型 sht_temperature/sht_humidity） */
        if (sn.sht_valid != 0U) {
            snprintf(s_report_extra, sizeof(s_report_extra), ",\"sht_temperature\":%d.%u,\"sht_humidity\":%u.%u",
                     (int)sn.sht_temp_int, sn.sht_temp_dec, (unsigned)sn.sht_hum_int, (unsigned)sn.sht_hum_dec);
        } else {
            s_report_extra[0] = '\0';
        }
        /* QMI8658 姿态与六轴（物模型 pitch/roll/vibration/ax/ay/az/gx/gy/gz） */
        if (sn.imu_valid != 0U) {
            /* 从 diag snapshot 读振动模值 g */
            APP_Diag_Snapshot_t diag_snap;
            APP_DIAG_GetSnapshot(&diag_snap);
            int vib_x100 = (int)(diag_snap.vibration * 100.0f);
            snprintf(s_report_extra + strlen(s_report_extra),
                     sizeof(s_report_extra) - strlen(s_report_extra),
                     ",\"pitch\":%d.%u,\"roll\":%d.%u,\"vibration\":%d.%02d,\"ax\":%d,\"ay\":%d,\"az\":%d,\"gx\":%d,\"gy\":%d,\"gz\":%d",
                     (int)sn.imu_pitch_int, sn.imu_pitch_dec, (int)sn.imu_roll_int, sn.imu_roll_dec,
                     vib_x100/100, vib_x100%100,
                     (int)sn.imu_ax_mg, (int)sn.imu_ay_mg, (int)sn.imu_az_mg,
                     (int)sn.imu_gx_mdps, (int)sn.imu_gy_mdps, (int)sn.imu_gz_mdps);
        }
        /* INA226 电源（物模型 voltage/current/power，单位 V/A/W） */
        if (sn.pwr_valid != 0U) {
            /* mV → V, mA → A, mW → W（除以1000，转成小数） */
            int volt_int = (int)(sn.pwr_bus_mv / 1000U);
            int volt_dec = (int)(sn.pwr_bus_mv % 1000U);
            int curr_int = (int)(sn.pwr_current_ma / 1000U);
            int curr_dec = (int)(sn.pwr_current_ma % 1000U);
            int pwr_int  = (int)(sn.pwr_power_mw / 1000U);
            int pwr_dec  = (int)(sn.pwr_power_mw % 1000U);
            snprintf(s_report_extra + strlen(s_report_extra),
                     sizeof(s_report_extra) - strlen(s_report_extra),
                     ",\"voltage\":%d.%03d,\"current\":%d.%03d,\"power\":%d.%03d",
                     volt_int, volt_dec, curr_int, curr_dec, pwr_int, pwr_dec);
        }
        /* 继电器状态（物模型 relay_status） */
        if (sn.relay_valid != 0U) {
            snprintf(s_report_extra + strlen(s_report_extra),
                     sizeof(s_report_extra) - strlen(s_report_extra),
                     ",\"relay_status\":%u", (unsigned)sn.relay_on);
        }
#if APP_DIAG_ENABLE && APP_ALARM_ENABLE && APP_FAULT_ENABLE
        /* 告警状态 + 故障诊断（物模型 alarm_status/diag_status/diag_mask） */
        {
            FaultDiagnosis_t diag;
            APP_FAULT_GetResult(&diag);
            snprintf(s_report_extra + strlen(s_report_extra),
                     sizeof(s_report_extra) - strlen(s_report_extra),
                     ",\"alarm_status\":%u,\"diag_status\":%u,\"diag_mask\":%u",
                     (unsigned)g_alarm_active,
                     (unsigned)diag.status, (unsigned)diag.fault_mask);
        }
#endif
    }
#else
    s_report_extra[0] = '\0';
#endif

    /* ── 组装华为云物模型 JSON ── */
    len = snprintf(payload, buf_size,
                   "{\"services\":[{\"service_id\":\"%s\",\"properties\":{",
                   HUAWEI_SERVICE_ID);

    {
        uint8_t first = 1U;
        if (has_temp != 0U) {
            len += snprintf(payload + len, buf_size - (size_t)len,
                            "\"temperature\":%u.%u", t_int, t_dec);
            first = 0U;
        }
        if (has_hum != 0U) {
            if (first == 0U) {
                len += snprintf(payload + len, buf_size - (size_t)len, ",");
            }
            len += snprintf(payload + len, buf_size - (size_t)len,
                            "\"humidity\":%u.%u", h_int, h_dec);
            first = 0U;
        }
        if (has_light != 0U) {
            if (first == 0U) {
                len += snprintf(payload + len, buf_size - (size_t)len, ",");
            }
            len += snprintf(payload + len, buf_size - (size_t)len,
                            "\"light\":%u", light_adc);
            first = 0U;
        }
        /* 毕设扩展属性（s_report_extra 已带前导逗号；若上面有属性则直接拼接） */
        if (s_report_extra[0] != '\0') {
            if (first != 0U) {
                /* 无基础属性时去掉前导逗号 */
                len += snprintf(payload + len, buf_size - (size_t)len, "%s", s_report_extra + 1);
            } else {
                len += snprintf(payload + len, buf_size - (size_t)len, "%s", s_report_extra);
            }
        }
    }

    len += snprintf(payload + len, buf_size - (size_t)len, "}}]}");

    if ((len <= 0) || ((size_t)len >= buf_size)) {
        MQTT_Printf("[MQTT] JSON build failed\r\n");
        return -1;
    }
    return len;
}

/**
 * @brief  组装并发布一次传感器数据（华为云物模型JSON格式）
 * @retval 1=成功, 0=失败
 */
static uint8_t mqtt_publish_once(void)
{
    static uint32_t s_last_pub_tick = 0U;
    char payload[512];   /*!< JSON缓冲（毕设扩展后属性较多，需512字节） */
    int  len;
    MQTTMessage msg;
    int rc;

    /* 发布间隔监视：超过 5s 未成功发布说明任务被阻塞/网络中断，打点定位 */
    {
        uint32_t now_tick = (uint32_t)osKernelGetTickCount();
        if ((s_last_pub_tick != 0U) && ((now_tick - s_last_pub_tick) > 5000U)) {
            MQTT_Printf("[MQTT] publish gap %ums (stalled?)\r\n",
                             (unsigned)(now_tick - s_last_pub_tick));
        }
        s_last_pub_tick = now_tick;
    }

    len = mqtt_build_payload(payload, sizeof(payload));
    if (len <= 0) {
        return 0U;
    }

    /* 填充 MQTT 消息 */
    msg.qos        = (enum QoS)MQTT_QOS;
    msg.retained   = 0;
    msg.dup        = 0;
    msg.id         = 0;
    msg.payload    = payload;
    msg.payloadlen = (size_t)len;

    rc = MQTTPublish(&s_mqtt_client, s_topic_pub, &msg);
    if (rc != 0) {
        MQTT_Printf("[MQTT] Publish FAILED rc=%d, caching for retry\r\n", rc);
        offline_cache_push(payload, (uint8_t)len);
        return 0U;
    }

    MQTT_Printf("[MQTT] Published: %s\r\n", payload);
    return 1U;
}

#if APP_OFFLINE_ENABLE && APP_DIAG_ENABLE && APP_FAULT_ENABLE
/**
 * @brief  重连成功后补发断网期间缓存的传感器数据（最多50条，每条间隔50ms）
 * @note   缓存快照为 float 格式，发布时转回华为云物模型整数/拆分格式：
 *         温度/湿度 int.dec，电压 mV，电流 mA，功率 mW，告警/诊断整数
 */
static void mqtt_publish_backfill(void)
{
    APP_Diag_Snapshot_t snap;
    FaultDiagnosis_t diag;
    uint16_t sent = 0U;

    APP_FAULT_GetResult(&diag);

    while ((APP_OFFLINE_Pop(&snap) != 0U) && (sent < 50U)) {
        char payload[384];
        int len;
        MQTTMessage msg;
        int t_int, t_dec, h_int, h_dec;

        /* 温度/湿度转 int.dec（温度含符号） */
        t_int = (int)snap.temperature;
        t_dec = (int)((snap.temperature - (float)t_int) * 10.0f);
        if (t_dec < 0) t_dec = -t_dec;
        h_int = (int)snap.humidity;
        h_dec = (int)((snap.humidity - (float)h_int) * 10.0f);

        len = snprintf(payload, sizeof(payload),
            "{\"services\":[{\"service_id\":\"%s\",\"properties\":{"
            "\"temperature\":%d.%u,\"humidity\":%u.%u,"
            "\"bus_voltage\":%u,\"current\":%d,\"power\":%u,"
            "\"alarm\":%u,\"diag_status\":%u,\"diag_mask\":%u}}]}",
            HUAWEI_SERVICE_ID,
            t_int, (unsigned)t_dec, h_int, (unsigned)h_dec,
            (unsigned)((snap.voltage >= 0.0f) ? (uint32_t)(snap.voltage * 1000.0f) : 0U),
            (int)((snap.current >= 0.0f) ? (int32_t)(snap.current * 1000.0f) : 0),
            (unsigned)((snap.power >= 0.0f) ? (uint32_t)(snap.power * 1000.0f) : 0U),
            (unsigned)g_alarm_active,
            (unsigned)diag.status, (unsigned)diag.fault_mask);
        if (len <= 0) break;

        memset(&msg, 0, sizeof(msg));
        msg.qos        = MQTT_QOS;
        msg.retained   = 0;
        msg.dup        = 0;
        msg.id         = 0;
        msg.payload    = payload;
        msg.payloadlen = (size_t)len;

        if (MQTTPublish(&s_mqtt_client, s_topic_pub, &msg) != 0) {
            MQTT_Printf("[MQTT] Backfill interrupted by disconnect\r\n");
            break;
        }
        sent++;
        osDelay(50U);
    }

    if (sent > 0U) {
        MQTT_Printf("[MQTT] Backfilled %u cached records\r\n", (unsigned)sent);
    }
}
#endif /* APP_OFFLINE_ENABLE && APP_DIAG_ENABLE && APP_FAULT_ENABLE */

/* ==========================================================================
 *  任务入口
 * ========================================================================== */

void APP_MQTT_Task(void *argument)
{
    (void)argument;
    uint8_t connected = 0U;

    MQTT_Printf("[MQTT] Task started, broker=%s:%u, publish every %ums\r\n",
                     MQTT_BROKER_HOST, (unsigned)MQTT_BROKER_PORT,
                     (unsigned)MQTT_PUBLISH_PERIOD_MS);
    MQTT_Printf("[MQTT] Huawei IoTDA device_id=%s\r\n", HUAWEI_DEVICE_ID);

    /* 等待其他任务初始化（传感器等） */
    osDelay(500U);

    for (;;) {
#if APP_WATCHDOG_ENABLE
        Watchdog_Kick(WDT_TASK_MQTT);
#endif
        /* ── 阶段 A：未连接 → 执行分段连接链路 ── */
        if (connected == 0U) {
            uint32_t backoff;

            s_mqtt_status = 0U;
            MQTT_Printf("[MQTT] ===== Connecting to Huawei Cloud (fail=%u) =====\r\n",
                             (unsigned)s_connect_fail_count);

            /* 连续失败达到阈值：复位 ESP8266 模块自愈（AT 能回但连不上网的异常状态） */
            if (s_connect_fail_count >= MQTT_MAX_FAIL_BEFORE_RESET) {
                MQTT_Printf("[MQTT] Consecutive failures=%u, reset ESP8266...\r\n",
                                 (unsigned)s_connect_fail_count);
                (void)BSP_ESP8266_Reset();
#if APP_WATCHDOG_ENABLE
                Watchdog_DelayWithKick(WDT_TASK_MQTT, 2000U);
#else
                osDelay(2000U);
#endif
                s_connect_fail_count = 0U;  /* 复位后重新计数 */
            }

            if (mqtt_full_connect() != 0U) {
                connected = 1U;
                s_connect_fail_count = 0U;  /* 连接成功，重置失败计数 */
                MQTT_Printf("[MQTT] ===== Network recovered, publishing latest data =====\r\n");
                /* 网络恢复：先发布最新传感器数据（确保云端看到当前状态） */
                (void)mqtt_publish_once();
                /* 再批量重传离线缓存（断网期间积累的数据） */
                (void)mqtt_flush_offline_cache(&s_mqtt_client);
#if APP_OFFLINE_ENABLE && APP_DIAG_ENABLE && APP_FAULT_ENABLE
                /* 最后补发 app_offline 断网快照缓存（毕设断网补传链） */
                if (APP_OFFLINE_Count() > 0U) {
                    mqtt_publish_backfill();
                }
#endif
            } else {
                s_connect_fail_count++;
                backoff = mqtt_backoff_delay();
                MQTT_Printf("[MQTT] Connect failed (count=%u), retry in %ums (offline caching active)...\r\n",
                                 (unsigned)s_connect_fail_count, (unsigned)backoff);
                /* 断网退避期间：按 MQTT_PUBLISH_PERIOD_MS 采集一次传感器数据并缓存，
                   避免网络恢复后丢失这段时间的数据 */
                {
                    uint32_t bo_waited = 0U;
                    while (bo_waited < backoff) {
                        uint32_t slice = backoff - bo_waited;
                        if (slice > MQTT_PUBLISH_PERIOD_MS) {
                            slice = MQTT_PUBLISH_PERIOD_MS;
                        }
                        /* 拆成500ms小段喂狗 */
                        uint32_t sw = 0U;
                        while (sw < slice) {
                            uint32_t tick = (slice - sw < 500U) ? (slice - sw) : 500U;
#if APP_WATCHDOG_ENABLE
                            Watchdog_DelayWithKick(WDT_TASK_MQTT, tick);
#else
                            osDelay(tick);
#endif
                            sw += tick;
                        }
                        bo_waited += slice;
                        /* 采集数据并缓存到离线队列 */
                        {
                            char off_payload[256];
                            int off_len = mqtt_build_payload(off_payload, sizeof(off_payload));
                            if (off_len > 0) {
                                offline_cache_push(off_payload, (uint8_t)off_len);
                                MQTT_Printf("[MQTT] Offline cached (%u/%u queued)\r\n",
                                                 (unsigned)s_offline_count, (unsigned)MQTT_OFFLINE_CACHE_MAX);
                            }
                        }
                    }
                }
                continue;
            }
        }

        /* ── 阶段 B：已连接 → 发布属性数据（失败重试1次，仍失败则重连） ── */
        if (mqtt_publish_once() == 0U) {
            /* 第一次失败：可能是瞬时错误，重试一次 */
            osDelay(200U);
            if (mqtt_publish_once() == 0U) {
                /* 重试仍失败：连接已断，关 TCP 重连（payload 已在第一次失败时缓存） */
                connected = 0U;
                (void)BSP_ESP8266_TCPClose();
                MQTT_Printf("[MQTT] Publish failed (retry), reconnect...\r\n");
                s_connect_fail_count++;
                osDelay(mqtt_backoff_delay());
                continue;
            }
        }

        /* ── 阶段 C：在上报周期内调用 MQTTYield（维持心跳 + 处理下行命令）
         *    必须频繁调用，间隔不能超过 keepalive/2，否则 Broker 会主动断开 ── */
        {
            uint32_t waited = 0U;
            int rc;
            while (waited < MQTT_PUBLISH_PERIOD_MS) {
                /* 断线感知不依赖 AT+CIPSTATUS 轮询：MQTT 二进制数据模式下
                 * 发 CIPSTATUS 会与数据流混流（wait_response 会消费二进制数据），
                 * 且同步阻塞最长 2s 会拉长发布周期。改用三重异步机制：
                 *   1) WIFI DISCONNECT 异步事件（s_wifi_closed，下方立即检查）
                 *   2) TCPRead 检测到 "CLOSED"（s_tcp_closed，Yield 返回-1）
                 *   3) Paho MQTT keepalive：半开静默连接由 PINGREQ/PINGRESP 超时发现
                 */
                /* WiFi 热点断开异步事件：立即重连（不等 10s TCP 探测） */
                if (BSP_ESP8266_IsWiFiClosed() != 0U) {
                    MQTT_Printf("[MQTT] WiFi link lost (async WIFI DISCONNECT), reconnect\r\n");
                    connected = 0U;
                    (void)BSP_ESP8266_TCPClose();
                    BSP_ESP8266_ClearWiFiClosedFlag();
                    s_connect_fail_count++;
#if APP_WATCHDOG_ENABLE
                    Watchdog_DelayWithKick(WDT_TASK_MQTT, mqtt_backoff_delay());
#else
                    osDelay(mqtt_backoff_delay());
#endif
                    break;
                }
                rc = MQTTYield(&s_mqtt_client, MQTT_YIELD_TIMEOUT_MS);
                if (rc != 0) {
                    /* Yield 失败（通常是连接断开） */
                    connected = 0U;
                    (void)BSP_ESP8266_TCPClose();
                    MQTT_Printf("[MQTT] Yield failed rc=%d, will reconnect...\r\n", rc);
                    s_connect_fail_count++;
#if APP_WATCHDOG_ENABLE
                    Watchdog_DelayWithKick(WDT_TASK_MQTT, mqtt_backoff_delay());
#else
                    osDelay(mqtt_backoff_delay());
#endif
                    break;
                }
                /* 处理回调中缓存的原始命令（JSON解析/LED控制/继电器/EEPROM写，不阻塞网络栈） */
                mqtt_process_command();
                /* 发布命令响应 */
                mqtt_flush_reply(&s_mqtt_client);
                /* 发布外部发布队列（遥测/影子等上行的数据） */
                mqtt_flush_ext_publish(&s_mqtt_client);
#if APP_WATCHDOG_ENABLE
                Watchdog_Kick(WDT_TASK_MQTT);  /* yield循环中定期心跳 */
#endif
                waited += (uint32_t)MQTT_YIELD_TIMEOUT_MS;
            }
        }
    }
}

#endif /* APP_MQTT_ENABLE && APP_TASKS_ENABLE && BSP_ESP8266_ENABLE */
