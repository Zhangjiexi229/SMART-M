/**
 ******************************************************************************
 * @file    app_mqtt.c
 * @brief   MQTT 上云任务 — ESP8266 + Paho MQTT 连接华为云 IoTDA
 *          周期上报温湿度属性 + 接收云平台命令控制LED + 回复命令响应
 *
 *  华为云主题：
 *    上报属性(发布): $oc/devices/{device_id}/sys/properties/report
 *    接收命令(订阅): $oc/devices/{device_id}/sys/commands/#
 *    命令响应(发布): $oc/devices/{device_id}/sys/commands/response/request_id={rid}
 *
 *  华为云物模型 JSON：
 *    上报: {"services":[{"service_id":"Sensor","properties":{
 *            "temperature":25.5,"humidity":60.0,           ← DHT11（或SHT30补充）
 *            "sht_temperature":26.3,"sht_humidity":58.0,   ← SHT30
 *            "pitch":1.2,"roll":0.5,"ax":12,"ay":3,"az":1000,"gx":5,"gy":-2,"gz":8, ← QMI8658
 *            "bus_voltage":24000,"current":1200,"power":28800, ← INA226（mV/mA/mW）
 *            "relay_state":1}}]}                            ← 继电器
 *    命令: {"request_id":"xxx","service_id":"Sensor","command_name":"LED_Control","paras":{"led":1}}
 *          {"request_id":"xxx","service_id":"Sensor","command_name":"RELAY_Control","paras":{"relay":1}}
 *    响应: {"result_code":0,"response_name":"COMMAND_RESPONSE","paras":{"result":"success"}}
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_MQTT_ENABLE && APP_TASKS_ENABLE && BSP_ESP8266_ENABLE

#include "app_mqtt.h"
#include "mqtt_port.h"
#include "bsp_esp8266.h"
#include "bsp_uart.h"
#if BSP_LED_ENABLE
#include "bsp_led.h"
#endif
#include "cmsis_os.h"
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

/* 下行指令处理：文本协议 + 十六进制帧（与 app_wifi 远程控灯同构） */
#if PROTOCOL_ENABLE
#include "protocol.h"
#include "protocol_hex.h"
#if BSP_LED_ENABLE
#include "app_cmd.h"
#endif
#include "app_hex_cmd.h"
#endif /* PROTOCOL_ENABLE */

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

/* 华为云主题缓冲（运行时拼接 device_id） */
static char s_topic_pub[128];    /*!< 上报属性主题 */
static char s_topic_sub[128];    /*!< 订阅命令主题 */
static char s_topic_resp[160];   /*!< 命令响应主题（含request_id） */

/* 待回复缓冲：在 mqtt_message_handler 回调中填充，在任务主循环中发布。
 * 不能在回调内直接调用 MQTTPublish——回调运行在 MQTTYield 的读取上下文中，
 * 重入会破坏 paho 的接收状态机。 */
#define MQTT_REPLY_BUF_SIZE  128U
static uint8_t           s_reply_payload[MQTT_REPLY_BUF_SIZE];
static uint16_t          s_reply_len;
static volatile uint8_t  s_reply_pending;
static char              s_reply_request_id[64];  /*!< 华为云命令的request_id，响应时需原样返回 */

/* 毕设扩展属性 JSON 片段（SHT30/QMI8658/INA226/继电器），
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
 * @brief  执行LED控制命令
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
        BSP_UART1_Printf("[MQTT] Command response publish failed\r\n");
    } else {
        BSP_UART1_Printf("[MQTT] Command response sent (request_id=%s)\r\n", s_reply_request_id);
    }
}

/**
 * @brief  收到订阅主题的消息时被 Paho 调用（在 MQTTYield 上下文中）
 *
 *  华为云命令下发 payload 格式：
 *    {"request_id":"xxx","service_id":"Sensor","command_name":"LED_Control","paras":{"led":1}}
 *
 *  处理流程：
 *    1. 从主题提取 request_id
 *    2. 从 JSON 提取 command_name 和 paras.led
 *    3. 执行 LED 控制
 *    4. 构建响应 JSON 并缓存（不在回调内直接发布）
 *
 * @param  md 包含主题名和消息内容
 */
static void mqtt_message_handler(MessageData *md)
{
    MQTTMessage *msg = md->message;
    char payload_str[256];
    char command_name[32];
    char request_id[64];
    int  led_value = 0;
    char response[128];
    uint16_t resp_len;

    /* 打印原始消息（调试用） */
    BSP_UART1_Printf("[MQTT] RECV topic=%.*s len=%u\r\n",
                     (int)md->topicName->lenstring.len,
                     md->topicName->lenstring.data,
                     (unsigned)msg->payloadlen);
    BSP_UART1_Printf("[MQTT] payload: %.*s\r\n",
                     (int)msg->payloadlen, (char *)msg->payload);

    /* 拷贝 payload 到字符串缓冲区（确保以'\0'结尾） */
    if (msg->payloadlen >= sizeof(payload_str)) {
        BSP_UART1_Printf("[MQTT] payload too long, ignored\r\n");
        return;
    }
    memcpy(payload_str, msg->payload, msg->payloadlen);
    payload_str[msg->payloadlen] = '\0';

    /* 从主题提取 request_id */
    if (huawei_extract_request_id(md->topicName->lenstring.data, request_id, sizeof(request_id)) == 0U) {
        BSP_UART1_Printf("[MQTT] request_id not found in topic, ignored\r\n");
        return;
    }

    /* 从 JSON 提取 command_name */
    if (json_get_string(payload_str, "command_name", command_name, sizeof(command_name)) == 0U) {
        BSP_UART1_Printf("[MQTT] command_name not found, ignored\r\n");
        return;
    }

    BSP_UART1_Printf("[MQTT-CMD] Huawei command: %s, request_id=%s\r\n", command_name, request_id);

    /* 处理 LED_Control 命令 */
    if (strcmp(command_name, "LED_Control") == 0) {
        if (json_get_int(payload_str, "led", &led_value) != 0U) {
            BSP_UART1_Printf("[MQTT-CMD] LED_Control led=%d\r\n", led_value);
            huawei_exec_led(led_value);

            /* 构建成功响应 */
            resp_len = (uint16_t)snprintf(response, sizeof(response),
                "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"success\",\"led\":%d}}",
                led_value);
            mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
        } else {
            /* 参数缺失，返回失败响应 */
            resp_len = (uint16_t)snprintf(response, sizeof(response),
                "{\"result_code\":1,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"missing parameter 'led'\"}}");
            mqtt_queue_reply((uint8_t *)response, resp_len, request_id);
        }
    }
    /* 处理 RELAY_Control 命令（毕设：远程控制电机电源通断） */
    else if (strcmp(command_name, "RELAY_Control") == 0) {
        int relay_value = 0;
        if (json_get_int(payload_str, "relay", &relay_value) != 0U) {
            BSP_UART1_Printf("[MQTT-CMD] RELAY_Control relay=%d\r\n", relay_value);
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
        /* 未知命令 */
        BSP_UART1_Printf("[MQTT-CMD] Unknown command: %s\r\n", command_name);
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

/* ==========================================================================
 *  内部辅助函数
 * ========================================================================== */

/**
 * @brief  完整连接链路：WiFi → TCP → MQTT CONNECT → 订阅
 * @retval 1=全部成功，0=失败
 */
static uint8_t mqtt_full_connect(void)
{
    int rc;
    MQTTPacket_connectData connect_data = MQTTPacket_connectData_initializer;

    /* ── 1. AT 测试（模块在线？） ── */
    BSP_UART1_Printf("[MQTT] AT test...\r\n");
    if (BSP_ESP8266_TestAT(2000U) != ESP8266_OK) {
        BSP_UART1_Printf("[MQTT] AT test FAILED\r\n");
        return 0U;
    }
    BSP_UART1_Printf("[MQTT] AT OK\r\n");

    /* ── 2. 设为 Station 模式 ── */
    if (BSP_ESP8266_SetMode(ESP8266_MODE_STA) != ESP8266_OK) {
        BSP_UART1_Printf("[MQTT] Set STA mode FAILED\r\n");
        return 0U;
    }

    /* ── 3. 连接 WiFi 路由器 ── */
    BSP_UART1_Printf("[MQTT] Joining AP \"%s\" ...\r\n", MQTT_WIFI_SSID);
    if (BSP_ESP8266_JoinAP(MQTT_WIFI_SSID, MQTT_WIFI_PASSWORD) != ESP8266_OK) {
        BSP_UART1_Printf("[MQTT] WiFi join FAILED. Check SSID/password/2.4GHz\r\n");
        return 0U;
    }
    s_mqtt_status |= 0x01U;
    BSP_UART1_Printf("[MQTT] WiFi connected\r\n");
    osDelay(300U);  /* 等待 DHCP 稳定 */

    /* ── 4. TCP 连接到 MQTT Broker（内部会切换到 MQTT 二进制接收模式） ── */
    BSP_UART1_Printf("[MQTT] TCP connecting %s:%u ...\r\n",
                     MQTT_BROKER_HOST, (unsigned)MQTT_BROKER_PORT);
    NetworkInit(&s_mqtt_network);
    if (NetworkConnect(&s_mqtt_network, (char *)MQTT_BROKER_HOST, (int)MQTT_BROKER_PORT) != 0) {
        BSP_UART1_Printf("[MQTT] TCP connect FAILED. Check broker address/port/firewall\r\n");
        return 0U;
    }
    s_mqtt_status |= 0x02U;
    BSP_UART1_Printf("[MQTT] TCP connected\r\n");

    /* ── 5. MQTT 客户端初始化 ── */
    MQTTClientInit(&s_mqtt_client, &s_mqtt_network,
                   5000U,                          /* 命令超时 5 秒 */
                   s_mqtt_sendbuf, sizeof(s_mqtt_sendbuf),
                   s_mqtt_readbuf, sizeof(s_mqtt_readbuf));

    /* ── 6. 发送 MQTT CONNECT 报文（华为云密钥鉴权） ── */
    connect_data.MQTTVersion           = 4;   /* MQTT 3.1.1 */
    connect_data.clientID.cstring      = (char *)MQTT_CLIENT_ID;
    connect_data.keepAliveInterval     = MQTT_KEEPALIVE_SEC;
    connect_data.cleansession          = 1;
    connect_data.username.cstring      = (char *)MQTT_USERNAME;
    connect_data.password.cstring      = (char *)MQTT_PASSWORD;

    rc = MQTTConnect(&s_mqtt_client, &connect_data);
    if (rc != 0) {
        BSP_UART1_Printf("[MQTT] MQTT CONNECT FAILED rc=%d (check ClientId/Username/Password)\r\n", rc);
        return 0U;
    }
    s_mqtt_status |= 0x04U;
    BSP_UART1_Printf("[MQTT] MQTT connected (client=%s)\r\n", MQTT_CLIENT_ID);

    /* ── 7. 拼接华为云主题 ── */
    huawei_build_topics();

    /* ── 8. 订阅命令主题 ── */
    rc = MQTTSubscribe(&s_mqtt_client, s_topic_sub, (enum QoS)MQTT_QOS, mqtt_message_handler);
    if (rc != 0) {
        BSP_UART1_Printf("[MQTT] Subscribe FAILED rc=%d, continue without subscription\r\n", rc);
    } else {
        BSP_UART1_Printf("[MQTT] Subscribed to \"%s\" (QoS=%d)\r\n", s_topic_sub, MQTT_QOS);
    }

    return 1U;
}

/**
 * @brief  组装并发布一次传感器数据（华为云物模型JSON格式）
 * @retval 1=成功，0=失败
 *
 *  华为云属性上报 JSON 格式：
 *    {"services":[{"service_id":"Sensor","properties":{"temperature":25.5,"humidity":60.0}}]}
 *
 *  service_id 必须与控制台"功能定义"中的服务ID一致
 *  properties 中的键名必须与定义的属性名一致
 */
static uint8_t mqtt_publish_once(void)
{
    char payload[512];   /*!< JSON缓冲（毕设扩展后属性较多，需512字节） */
    int  len = 0;
    MQTTMessage msg;
    int rc;

    /* 传感器数据缓存（先读取再组装，避免条件编译嵌套过深） */
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
        /* SHT30 独立属性（物模型新增 sht_temperature/sht_humidity） */
        if (sn.sht_valid != 0U) {
            snprintf(s_report_extra, sizeof(s_report_extra), ",\"sht_temperature\":%d.%u,\"sht_humidity\":%u.%u",
                     (int)sn.sht_temp_int, sn.sht_temp_dec, (unsigned)sn.sht_hum_int, (unsigned)sn.sht_hum_dec);
        } else {
            s_report_extra[0] = '\0';
        }
        /* QMI8658 姿态与六轴（物模型 pitch/roll/ax/ay/az/gx/gy/gz/vibration） */
        if (sn.imu_valid != 0U) {
            /* 从 diag snapshot 读振动模值g */
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
        /* INA226 电源（物模型 voltage/current/power，单位V/A/W） */
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
    len = snprintf(payload, sizeof(payload),
                   "{\"services\":[{\"service_id\":\"%s\",\"properties\":{",
                   HUAWEI_SERVICE_ID);

    {
        uint8_t first = 1U;
        if (has_temp != 0U) {
            len += snprintf(payload + len, sizeof(payload) - (size_t)len,
                            "\"temperature\":%u.%u", t_int, t_dec);
            first = 0U;
        }
        if (has_hum != 0U) {
            if (first == 0U) {
                len += snprintf(payload + len, sizeof(payload) - (size_t)len, ",");
            }
            len += snprintf(payload + len, sizeof(payload) - (size_t)len,
                            "\"humidity\":%u.%u", h_int, h_dec);
            first = 0U;
        }
        if (has_light != 0U) {
            if (first == 0U) {
                len += snprintf(payload + len, sizeof(payload) - (size_t)len, ",");
            }
            len += snprintf(payload + len, sizeof(payload) - (size_t)len,
                            "\"light\":%u", light_adc);
            first = 0U;
        }
        /* 毕设扩展属性（s_report_extra 已带前导逗号；若上面有属性则直接拼接） */
        if (s_report_extra[0] != '\0') {
            if (first != 0U) {
                /* 无基础属性时去掉前导逗号 */
                len += snprintf(payload + len, sizeof(payload) - (size_t)len, "%s", s_report_extra + 1);
            } else {
                len += snprintf(payload + len, sizeof(payload) - (size_t)len, "%s", s_report_extra);
            }
        }
    }

    len += snprintf(payload + len, sizeof(payload) - (size_t)len, "}}]}");

    if ((len <= 0) || ((size_t)len >= sizeof(payload))) {
        BSP_UART1_Printf("[MQTT] JSON build failed\r\n");
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
        BSP_UART1_Printf("[MQTT] Publish FAILED rc=%d\r\n", rc);
        return 0U;
    }

    BSP_UART1_Printf("[MQTT] Published: %s\r\n", payload);
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
            BSP_UART1_Printf("[MQTT] Backfill interrupted by disconnect\r\n");
            break;
        }
        sent++;
        osDelay(50U);
    }

    if (sent > 0U) {
        BSP_UART1_Printf("[MQTT] Backfilled %u cached records\r\n", (unsigned)sent);
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

    BSP_UART1_Printf("[MQTT] Task started, broker=%s:%u, publish every %ums\r\n",
                     MQTT_BROKER_HOST, (unsigned)MQTT_BROKER_PORT,
                     (unsigned)MQTT_PUBLISH_PERIOD_MS);
    BSP_UART1_Printf("[MQTT] Huawei IoTDA device_id=%s\r\n", HUAWEI_DEVICE_ID);

    /* 等待其他任务初始化（传感器等） */
    osDelay(500U);

    for (;;) {
        /* ── 阶段 A：未连接 → 执行完整连接链路 ── */
        if (connected == 0U) {
            s_mqtt_status = 0U;
            BSP_UART1_Printf("[MQTT] ===== Connecting to Huawei Cloud =====\r\n");

            if (mqtt_full_connect() != 0U) {
                connected = 1U;
                BSP_UART1_Printf("[MQTT] ===== All connected, start publishing =====\r\n");
#if APP_OFFLINE_ENABLE && APP_DIAG_ENABLE && APP_FAULT_ENABLE
                /* 断网期间缓存的数据补发 */
                if (APP_OFFLINE_Count() > 0U) {
                    mqtt_publish_backfill();
                }
#endif
            } else {
                BSP_UART1_Printf("[MQTT] Connect failed, retry in %ums...\r\n",
                                 (unsigned)MQTT_RECONNECT_DELAY_MS);
                osDelay(MQTT_RECONNECT_DELAY_MS);
                continue;
            }
        }

        /* ── 阶段 B：已连接 → 发布属性数据 ── */
        if (mqtt_publish_once() == 0U) {
            connected = 0U;
#if APP_OFFLINE_ENABLE && APP_DIAG_ENABLE
            /* 发布失败：缓存当前快照（断网补传） */
            {
                APP_Diag_Snapshot_t snap;
                APP_DIAG_GetSnapshot(&snap);
                (void)APP_OFFLINE_Push(&snap);
            }
#endif
            (void)MQTTDisconnect(&s_mqtt_client);
            BSP_UART1_Printf("[MQTT] Publish failed, will reconnect...\r\n");
            osDelay(MQTT_RECONNECT_DELAY_MS);
            continue;
        }

        /* ── 阶段 C：在上报周期内调用 MQTTYield（维持心跳 + 处理下行命令）
         *    必须频繁调用，间隔不能超过 keepalive/2，否则 Broker 会主动断开 ── */
        {
            uint32_t waited = 0U;
            int rc;
            while (waited < MQTT_PUBLISH_PERIOD_MS) {
                rc = MQTTYield(&s_mqtt_client, MQTT_YIELD_TIMEOUT_MS);
                if (rc != 0) {
                    /* Yield 失败（通常是连接断开） */
                    connected = 0U;
#if APP_OFFLINE_ENABLE && APP_DIAG_ENABLE
                    /* 缓存当前快照（断网补传） */
                    {
                        APP_Diag_Snapshot_t snap;
                        APP_DIAG_GetSnapshot(&snap);
                        (void)APP_OFFLINE_Push(&snap);
                    }
#endif
                    (void)MQTTDisconnect(&s_mqtt_client);
                    BSP_UART1_Printf("[MQTT] Yield failed rc=%d, will reconnect...\r\n", rc);
                    osDelay(MQTT_RECONNECT_DELAY_MS);
                    break;
                }
                /* 发布下行命令回调中缓存的响应（不在回调内直接发布，避免重入 MQTTPublish） */
                mqtt_flush_reply(&s_mqtt_client);
                waited += (uint32_t)MQTT_YIELD_TIMEOUT_MS;
            }
        }
    }
}

#endif /* APP_MQTT_ENABLE && APP_TASKS_ENABLE && BSP_ESP8266_ENABLE */
