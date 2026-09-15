/**
 ******************************************************************************
 * @file    app_netcfg.h
 * @brief   网络配置模块头文件 — WiFi 热点 + 华为云 IoTDA MQTT 连接参数
 *
 *  用途：
 *    - 把原本写死在 app_mqtt.h 里的编译期宏（SSID/密码/Broker/鉴权）提升为
 *      运行时配置，存储于内部 Flash（Sector7 @ 0x08060000），掉电不丢失
 *    - 小程序通过 BLE JSON 命令 GETCFG/SETCFG（见 app_bt24.c）读取/修改，
 *      保存后置"已修改"标记，MQTT 任务检测到后自动按新配置重连
 *
 *  数据流：
 *    小程序 SETCFG ──> app_bt24 解析到暂存配置 ──> APP_NetCfg_Apply()
 *        ├─ 临界区提交到 g_netcfg（避免与 MQTT 任务并发读撕裂）
 *        ├─ 写内部 Flash（掉电保持）
 *        └─ 置"已修改"标记 ──> APP_MQTT_Task 检测后重连
 *
 *  字段长度上限（含结束符）：
 *    wifi_ssid 32 / wifi_password 64 / broker_host 96 / broker_ip 16 /
 *    client_id 64 / username 64 / password 96 / device_id 64
 ******************************************************************************
 */
#ifndef APP_NETCFG_H
#define APP_NETCFG_H

#include <stdint.h>

/* ========== 字段长度上限（含结束符） ========== */
#define NETCFG_SSID_MAX       32U
#define NETCFG_WIFI_PWD_MAX   64U
#define NETCFG_HOST_MAX       96U
#define NETCFG_IP_MAX         16U
#define NETCFG_CLIENTID_MAX   64U
#define NETCFG_USERNAME_MAX   64U
#define NETCFG_MQTT_PWD_MAX   96U
#define NETCFG_DEVICEID_MAX   64U

/* ========== 网络配置结构体（1字节对齐，整块 512B 存 Flash） ========== */
#pragma pack(push, 1)
typedef struct {
    char     wifi_ssid[NETCFG_SSID_MAX];        /* WiFi 热点名（2.4GHz） */
    char     wifi_password[NETCFG_WIFI_PWD_MAX]; /* WiFi 密码 */
    char     broker_host[NETCFG_HOST_MAX];       /* MQTT Broker 域名（华为云 iot-mqtt 地址） */
    char     broker_ip[NETCFG_IP_MAX];           /* MQTT Broker IPv4 直连地址（空则用域名） */
    uint16_t broker_port;                        /* MQTT 端口（华为云非加密 1883） */
    char     client_id[NETCFG_CLIENTID_MAX];     /* MQTT clientId（设备密钥文件生成） */
    char     username[NETCFG_USERNAME_MAX];      /* MQTT username（= device_id） */
    char     password[NETCFG_MQTT_PWD_MAX];      /* MQTT password（鉴权密钥） */
    char     device_id[NETCFG_DEVICEID_MAX];     /* 华为云设备ID（主题拼接用） */
    uint8_t  reserved[12];                       /* 对齐/预留，保证整块 512 字节 */
} NetConfig_t;
#pragma pack(pop)

/* ========== 当前生效配置（MQTT 任务只读；写入请走 APP_NetCfg_Apply） ========== */
extern NetConfig_t g_netcfg;

/* ========== API ========== */

/**
 * @brief  加载网络配置：默认值（app_mqtt.h 宏）→ 内部Flash 覆盖
 * @note   由 APP_MQTT_Task 启动时调用一次
 */
void APP_NetCfg_Load(void);

/**
 * @brief  提交新配置：临界区内复制到 g_netcfg + 写内部Flash + 置修改标记
 * @param  new_cfg  新配置（需完整有效，缺失字段由调用方先复制当前值）
 * @retval 0=成功；非0=Flash 写入失败（配置仍生效，但重启后回退）
 * @note   Flash 128KB 扇区擦除约 1~2s，调用方应先喂看门狗；
 *         调用后 MQTT 任务会在下一轮循环自动按新配置重连
 */
uint8_t APP_NetCfg_Apply(const NetConfig_t *new_cfg);

/**
 * @brief  配置是否被修改（等待 MQTT 应用）
 * @retval 1=已修改, 0=无
 */
uint8_t APP_NetCfg_IsModified(void);

/**
 * @brief  清除"已修改"标记（MQTT 任务应用新配置后调用）
 */
void APP_NetCfg_ClearModified(void);

#endif /* APP_NETCFG_H */
