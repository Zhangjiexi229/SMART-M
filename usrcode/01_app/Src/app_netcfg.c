/**
 ******************************************************************************
 * @file    app_netcfg.c
 * @brief   网络配置模块实现 — WiFi + MQTT 云参数的运行时配置与 Flash 持久化
 *
 *  默认值取自 app_mqtt.h 编译期宏（保持出厂一致）；小程序 SETCFG 后
 *  通过 APP_NetCfg_Apply() 提交，写内部 Flash（Sector7），掉电不丢失。
 *
 *  并发安全：
 *    - g_netcfg 由 MQTT 任务只读；唯一写入路径是 APP_NetCfg_Apply()
 *    - Apply 在 osKernelLock 临界区内 memcpy 512B（微秒级），避免任务
 *      调度期间读到撕裂数据
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_NETCFG_ENABLE

#include "app_netcfg.h"
#include "app_mqtt.h"           /* 默认值宏：MQTT_WIFI_SSID 等 */
#include "bsp_flash_config.h"
#include "bsp_uart.h"
#include "cmsis_os.h"
#include <string.h>

/* ========== 全局配置 ========== */
NetConfig_t g_netcfg;

static volatile uint8_t s_netcfg_modified = 0U;   /* 1=有配置等待 MQTT 应用 */

/* ==========================================================================
 *  内部工具
 * ========================================================================== */

/* 从宏默认值填充（首次上电 / Flash 无有效配置时） */
static void netcfg_load_defaults(void)
{
    memset(&g_netcfg, 0, sizeof(g_netcfg));

    strncpy(g_netcfg.wifi_ssid, MQTT_WIFI_SSID, sizeof(g_netcfg.wifi_ssid) - 1U);
    strncpy(g_netcfg.wifi_password, MQTT_WIFI_PASSWORD, sizeof(g_netcfg.wifi_password) - 1U);
    strncpy(g_netcfg.broker_host, MQTT_BROKER_HOST, sizeof(g_netcfg.broker_host) - 1U);
#ifdef MQTT_BROKER_IP
    strncpy(g_netcfg.broker_ip, MQTT_BROKER_IP, sizeof(g_netcfg.broker_ip) - 1U);
#else
    g_netcfg.broker_ip[0] = '\0';
#endif
    g_netcfg.broker_port = MQTT_BROKER_PORT;
    strncpy(g_netcfg.client_id, MQTT_CLIENT_ID, sizeof(g_netcfg.client_id) - 1U);
    strncpy(g_netcfg.username, MQTT_USERNAME, sizeof(g_netcfg.username) - 1U);
    strncpy(g_netcfg.password, MQTT_PASSWORD, sizeof(g_netcfg.password) - 1U);
    strncpy(g_netcfg.device_id, HUAWEI_DEVICE_ID, sizeof(g_netcfg.device_id) - 1U);

    /* 保证结束符（宏值超长时截断，杜绝非 NUL 结尾） */
    g_netcfg.wifi_ssid[sizeof(g_netcfg.wifi_ssid) - 1U]     = '\0';
    g_netcfg.wifi_password[sizeof(g_netcfg.wifi_password) - 1U] = '\0';
    g_netcfg.broker_host[sizeof(g_netcfg.broker_host) - 1U] = '\0';
    g_netcfg.broker_ip[sizeof(g_netcfg.broker_ip) - 1U]     = '\0';
    g_netcfg.client_id[sizeof(g_netcfg.client_id) - 1U]     = '\0';
    g_netcfg.username[sizeof(g_netcfg.username) - 1U]       = '\0';
    g_netcfg.password[sizeof(g_netcfg.password) - 1U]       = '\0';
    g_netcfg.device_id[sizeof(g_netcfg.device_id) - 1U]     = '\0';
}

/* 对从 Flash 读出的配置做健壮性修正：保证各字符串 NUL 结尾 */
static void netcfg_sanitize(NetConfig_t *cfg)
{
    char *fields[] = {
        cfg->wifi_ssid,     cfg->wifi_password, cfg->broker_host, cfg->broker_ip,
        cfg->client_id,     cfg->username,      cfg->password,    cfg->device_id
    };
    uint32_t sizes[] = {
        sizeof(cfg->wifi_ssid),     sizeof(cfg->wifi_password),
        sizeof(cfg->broker_host),   sizeof(cfg->broker_ip),
        sizeof(cfg->client_id),     sizeof(cfg->username),
        sizeof(cfg->password),      sizeof(cfg->device_id)
    };
    uint32_t i;

    for (i = 0U; i < (sizeof(fields) / sizeof(fields[0])); i++) {
        /* 字段数组内没有 NUL（被写坏/超长）→ 强制截断 */
        if (memchr(fields[i], '\0', sizes[i]) == NULL) {
            fields[i][sizes[i] - 1U] = '\0';
        }
    }
    if (cfg->broker_port == 0U) {
        cfg->broker_port = MQTT_BROKER_PORT;
    }
}

/* ==========================================================================
 *  API
 * ========================================================================== */

void APP_NetCfg_Load(void)
{
    netcfg_load_defaults();

#if BSP_FLASH_CONFIG_ENABLE
    if (BSP_FLASH_LoadNetConfig((uint8_t *)&g_netcfg, sizeof(g_netcfg)) == 0U) {
        netcfg_sanitize(&g_netcfg);
        BSP_UART1_Printf("[NetCfg] Loaded from Flash: SSID=%s broker=%s:%u dev=%s\r\n",
                         g_netcfg.wifi_ssid, g_netcfg.broker_host,
                         (unsigned)g_netcfg.broker_port, g_netcfg.device_id);
    } else {
        BSP_UART1_Printf("[NetCfg] No valid config in Flash, using defaults\r\n");
    }
#else
    BSP_UART1_Printf("[NetCfg] Flash config disabled, using defaults\r\n");
#endif
}

uint8_t APP_NetCfg_Apply(const NetConfig_t *new_cfg)
{
    uint8_t ret;
    int32_t lock;

    if (new_cfg == NULL) {
        return 1U;
    }

    /* 临界区提交，避免 MQTT 任务读到撕裂字段 */
    lock = osKernelLock();
    memcpy(&g_netcfg, new_cfg, sizeof(g_netcfg));
    if (lock >= 0) {
        osKernelUnlock();
    }

    /* 写入内部 Flash（128KB 扇区擦除约 1~2s，调用方应先喂看门狗） */
#if BSP_FLASH_CONFIG_ENABLE
    ret = BSP_FLASH_SaveNetConfig((const uint8_t *)&g_netcfg, sizeof(g_netcfg));
#else
    ret = 1U;
#endif
    if (ret == 0U) {
        BSP_UART1_Printf("[NetCfg] Saved to Flash: SSID=%s broker=%s:%u dev=%s\r\n",
                         g_netcfg.wifi_ssid, g_netcfg.broker_host,
                         (unsigned)g_netcfg.broker_port, g_netcfg.device_id);
        s_netcfg_modified = 1U;   /* 通知 MQTT 任务按新配置重连 */
    } else {
        BSP_UART1_Printf("[NetCfg] Flash save FAILED (code=%u), config valid until reboot\r\n",
                         (unsigned)ret);
    }

    return ret;
}

uint8_t APP_NetCfg_IsModified(void)
{
    return s_netcfg_modified;
}

void APP_NetCfg_ClearModified(void)
{
    s_netcfg_modified = 0U;
}

#endif /* APP_NETCFG_ENABLE */
