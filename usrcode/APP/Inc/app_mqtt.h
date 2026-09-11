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
 *  与 APP_WIFI_ENABLE 互斥：共用 ESP8266，二选一
 ******************************************************************************
 */
#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdint.h>

/* ==========================================================================
 *  ★ 用户配置区 —— 请根据华为云实际环境修改 ★
 * ========================================================================== */

/* --- WiFi --- */
#define MQTT_WIFI_SSID        "your_wifi_ssid"  /*!< 2.4GHz WiFi 名称 */
#define MQTT_WIFI_PASSWORD    "your_wifi_password"             /*!< WiFi 密码 */

/* --- 华为云 IoTDA 接入参数（已从设备连接密钥文件填入） --- */
/* 注意：密钥文件给的是 MQTTS(8883/TLS)，ESP8266 AT方案用非加密 MQTT(1883)。
   华为云官方说明：非加密接入域名不变，仅将端口 8883 改为 1883 即可，
   不要把 iot-mqtts / iotda-device 改成 iot-mqtt（该域名不存在，会导致DNS解析失败） */
#define MQTT_BROKER_HOST      "your_mqtt_address.iotda-device.cn-north-4.myhuaweicloud.com"  /*!< 非加密MQTT接入地址（设备侧接入域名，已实测1883端口可达） */
#define MQTT_BROKER_PORT      1883U             /*!< MQTT 非加密端口 */
#define MQTT_CLIENT_ID        "your_client_id"  /*!< 连接密钥文件中的clientId */
#define MQTT_USERNAME         "your_username"    /*!< 连接密钥文件中的username（即device_id） */
#define MQTT_PASSWORD         "your_password"  /*!< 连接密钥文件中的password */
#define MQTT_KEEPALIVE_SEC    60U                /*!< 心跳间隔（秒） */

/* --- 华为云设备ID（用于主题拼接，与MQTT_USERNAME相同） --- */
#define HUAWEI_DEVICE_ID      "your_device_id"    /*!< 设备ID */

/* --- 华为云物模型服务ID（必须与控制台"功能定义"中的服务ID一致） --- */
#define HUAWEI_SERVICE_ID     "Sensor"           /*!< 物模型服务ID */

/* --- 主题（华为云固定格式，device_id 运行时拼接） --- */
#define MQTT_QOS              0                    /*!< QoS等级 0/1/2 */

/* --- 运行参数 --- */
#define MQTT_PUBLISH_PERIOD_MS  3000U   /*!< 数据上报周期（毫秒） */
#define MQTT_RECONNECT_DELAY_MS 3000U   /*!< 断线重连等待（毫秒） */
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

#endif /* APP_MQTT_H */
