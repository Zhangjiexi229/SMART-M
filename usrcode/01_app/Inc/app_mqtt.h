/**
 ******************************************************************************
 * @file    app_mqtt.h
 * @brief   MQTT 上云任务头文件 — ESP8266 + Paho MQTT 连接华为云 IoTDA
 *
 *  ╔══════════════════════════════════════════════════════════════════════╗
 *  ║  ★ 必须修改以下配置为你的华为云实际参数，否则无法连云 ★              ║
 *  ╠══════════════════════════════════════════════════════════════════════╣
 *  ║  1. 登录华为云 → 设备接入 IoTDA → 创建产品(协议MQTT,数据JSON)       ║
 *  ║  2. 产品→功能定义→添加服务(Sensor)→添加属性(temperature/humidity)  ║
 *  ║  3. 产品→功能定义→添加命令(LED_Control,参数led int)                 ║
 *  ║  4. 设备→添加设备→保存 device_id 和 device_secret                  ║
 *  ║  5. 总览→接入信息→复制 MQTT 接入地址                                ║
 *  ║  6. 用在线工具生成鉴权参数:                                          ║
 *  ║     https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/      ║
 *  ║     选"不校验时间戳",填入 device_id + device_secret                  ║
 *  ║  7. 把生成的 ClientId/Username/Password 填入下方                     ║
 *  ╚══════════════════════════════════════════════════════════════════════╝
 *
 *  华为云主题：
 *    上报属性(发布): $oc/devices/{device_id}/sys/properties/report
 *    接收命令(订阅): $oc/devices/{device_id}/sys/commands/#
 *    命令响应(发布): $oc/devices/{device_id}/sys/commands/response/request_id={rid}
 *
 *  任务流程：
 *    1. AT测试 → 设STA模式 → 连WiFi → TCP连接Broker → MQTT CONNECT → 订阅命令主题
 *    2. 周期发布传感器数据（华为云物模型JSON格式）
 *    3. MQTTYield 维持心跳 + 处理云平台下行命令（解析JSON→控制LED→回复响应）
 *    4. 任何环节失败 → 延迟3秒 → 从头重连
 *
 *  MQTT任务为唯一联网任务，独占 ESP8266
 ******************************************************************************
 */
#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdint.h>
/* ==========================================================================
 *  ★ 用户配置区 —— 请根据华为云实际环境修改 ★
 * ========================================================================== */

/* --- WiFi --- */
// 连接任一个能上互联网的wifi 热点 （主要是为了开发板通wifi模块，连接上华为云）
#define MQTT_WIFI_SSID        "your_wifi_ssid"        /* !< 2.4GHz WiFi 名称 */
#define MQTT_WIFI_PASSWORD    "your_wifi_password"    /*!< WiFi 密码 */

//----------------------------

/* --- 华为云 IoTDA 接入参数（已从设备连接密钥文件填入） --- */
/* 注意：密钥文件给的是 MQTTS(8883/TLS)，ESP8266 AT方案用非加密 MQTT(1883)。
   华为云官方说明：非加密接入域名不变，仅将端口 8883 改为 1883 即可，
   不要把 iot-mqtts 改成 iot-mqtt（该域名不存在，会导致DNS解析失败） */
#define MQTT_BROKER_HOST      "your_endpoint.iotda-device.cn-north-4.myhuaweicloud.com"  /*!< 非加密MQTT接入地址（域名与MQTTS相同） */
/* 可选：IPv4 直连兜底。本设备专属域名同时返回 IPv6(AAAA) 记录，部分 ESP8266 AT 固件
   解析此类域名会超时（现象：WiFi 连上后 TCP connect FAILED 循环重试）。
   定义为本域名的 IPv4 A 记录即可跳过模块 DNS 直接建连（2026-09 验证 A 记录=your_broker_ip）。
   若华为云调整地址导致连不上：重新解析域名更新此值，或注释掉本行改回域名直连。 */
#define MQTT_BROKER_IP        "your_broker_ip"
#define MQTT_BROKER_PORT      1883U             /*!< MQTT 非加密端口 */
#define MQTT_CLIENT_ID        "your_client_id"  /*!< 连接密钥文件中的clientId */
#define MQTT_USERNAME         "your_username"    /*!< 连接密钥文件中的username（即device_id） */
#define MQTT_PASSWORD         "your_password"  /*!< 连接密钥文件中的password */
#define MQTT_KEEPALIVE_SEC    60U                /*!< 心跳间隔（秒） */

/* --- 华为云设备ID（用于主题拼接，与MQTT_USERNAME相同） --- */
#define HUAWEI_DEVICE_ID      "your_username"    /*!< 设备ID */

/* --- 华为云物模型服务ID（必须与控制台"功能定义"中的服务ID一致） --- */
#define HUAWEI_SERVICE_ID     "Sensor"           /*!< 物模型服务ID */

/* --- 主题（华为云固定格式，device_id 运行时拼接） --- */
#define MQTT_QOS              0                    /*!< QoS等级 0/1/2 */
#define MQTT_OFFLINE_CACHE_MAX  16U                /*!< 离线缓存最大消息数（RAM环形缓冲） */

/* --- 运行参数 --- */
#define MQTT_PUBLISH_PERIOD_MS  3000U   /*!< 数据上报周期（毫秒） */
#define MQTT_RECONNECT_DELAY_MS 3000U   /*!< 兼容保留：实际重连等待采用指数退避（1000ms 起，上限30s），见 app_mqtt.c */
#define MQTT_YIELD_TIMEOUT_MS   200     /*!< 每次MQTTYield的超时（毫秒），越小下行响应越快 */

/* ==========================================================================
 *  任务参数
 * ========================================================================== */
#define MQTT_TASK_STACK_SIZE   (1024U * 4U)     /*!< 任务栈大小（字节），MQTT需较大栈 */
#define MQTT_TASK_PRIORITY     osPriorityNormal /*!< 任务优先级 */

/* ==========================================================================
 *  API
 * ========================================================================== */

/**
 * @brief  MQTT 上云任务入口
 * @param  argument 未使用
 * @note   由 app_tasks.c 创建为 FreeRTOS 任务
 */
void APP_MQTT_Task(void *argument);

/**
 * @brief  获取MQTT连接状态
 * @retval bit0=WiFi已连接, bit1=TCP已连接, bit2=MQTT已连接
 */
uint8_t APP_MQTT_GetStatus(void);

/**
 * @brief  外部发布接口（任意任务可调用，线程安全）
 * @param  payload  数据内容（华为云属性上报JSON格式，或其他MQTT载荷）
 * @param  len      数据长度
 * @retval 0=已入队（MQTT任务将发布/离线缓存）, 1=参数错误, 3=队列满（调用方自行缓冲）
 * @note   数据经线程安全队列进入 MQTT 任务发布通道，发布失败自动转入离线缓存重传；
 *         供 svc_conn 引擎（遥测/影子上报）等上层调用
 */
uint8_t APP_MQTT_Publish(const uint8_t *payload, uint16_t len);

/**
 * @brief  外部发布接口（指定主题短名）
 * @param  topic  主题短名："report"=属性上报, "messages/up"=设备消息上行（NULL等价"report"）
 * @param  payload  数据内容
 * @param  len      数据长度
 * @retval 同 APP_MQTT_Publish
 */
uint8_t APP_MQTT_PublishTopic(const char *topic, const uint8_t *payload, uint16_t len);

#endif /* APP_MQTT_H */
