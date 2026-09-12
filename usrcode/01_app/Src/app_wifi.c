/**
 ******************************************************************************
 * @file    app_wifi.c
 * @brief   WiFi 上报任务 — ESP8266 连路由器 + TCP 上报温湿度光照
 *
 *  任务状态机（简化为顺序初始化 + 运行循环）：
 *    1. 初始化：BSP_ESP8266_Init → AT 测试 → 设 STA 模式 → 连 WiFi → 查 IP → 连 TCP
 *    2. 运行：周期读取DHT11 + 光敏快照 →格式化→BSP_ESP8266_TCPSend 发送
 *    3. 异常：任何步骤失败 → 打印错误 → 延迟 3 秒 → 从该步骤重试
 *
 *  所有状态信息通过 USART1（BSP_UART1_Printf）打印，方便串口调试助手观察。
 *  USART3 专用于ESP8266 AT 通信，不做打印。
 *  上报数据格式（纯文本，便于网络调试助手直接查看）：
 *    [SENSOR] Temp=28.0C | Hum=49.0% | Light=1010/813mV/DARK
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_WIFI_ENABLE && APP_TASKS_ENABLE && BSP_ESP8266_ENABLE

#include "app_wifi.h"
#include "bsp_esp8266.h"
#include "bsp_uart.h"
#include "app_dht11.h"
#include "app_light_sensor.h"
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
#include "app_hc_sr04.h"
#endif
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
#include "app_eeprom.h"
#endif
#if APP_WIFI_LED_ENABLE && BSP_LED_ENABLE
#include "app_cmd.h"
#include "protocol.h"
#include "protocol_hex.h"
#include "app_hex_cmd.h"
#endif
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

/* ========== 内部宏 ========== */
#define WIFI_RETRY_DELAY_MS   3000U   /*!< 初始化失败后重试间隔 */
#define WIFI_MAX_AT_RETRY     3U      /*!< AT 测试连续失败次数（超过则复位模块） */

/* ========== 运行时配置（从EEPROM加载，失败则用宏默认值） ========== */
static char     s_wifi_ssid[32];
static char     s_wifi_password[64];
static char     s_tcp_server_ip[16];
static uint16_t s_tcp_server_port;
static uint16_t s_report_period_ms;

/* ========== 连接状态（供外部查询） ========== */
static volatile uint8_t s_wifi_status = 0U;  /* bit0=WiFi 已连接 bit1=TCP 已连接 */

uint8_t APP_WIFI_GetStatus(void)
{
    return s_wifi_status;
}

/**
 * @brief  从EEPROM加载WiFi配置，EEPROM 不可用或字段为空时用宏默认值 */
static void wifi_load_config(void)
{
    /* 先加载默认值 */
    strncpy(s_wifi_ssid, WIFI_SSID, sizeof(s_wifi_ssid) - 1U);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1U] = '\0';
    strncpy(s_wifi_password, WIFI_PASSWORD, sizeof(s_wifi_password) - 1U);
    s_wifi_password[sizeof(s_wifi_password) - 1U] = '\0';
    strncpy(s_tcp_server_ip, TCP_SERVER_IP, sizeof(s_tcp_server_ip) - 1U);
    s_tcp_server_ip[sizeof(s_tcp_server_ip) - 1U] = '\0';
    s_tcp_server_port = TCP_SERVER_PORT;
    s_report_period_ms = WIFI_REPORT_PERIOD_MS;

#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
    {
        const app_config_t *cfg = APP_EEPROM_GetConfig();
        if (cfg->wifi_ssid[0] != '\0') {
            strncpy(s_wifi_ssid, cfg->wifi_ssid, sizeof(s_wifi_ssid) - 1U);
            s_wifi_ssid[sizeof(s_wifi_ssid) - 1U] = '\0';
        }
        if (cfg->wifi_password[0] != '\0') {
            strncpy(s_wifi_password, cfg->wifi_password, sizeof(s_wifi_password) - 1U);
            s_wifi_password[sizeof(s_wifi_password) - 1U] = '\0';
        }
        if (cfg->tcp_server_ip[0] != '\0') {
            strncpy(s_tcp_server_ip, cfg->tcp_server_ip, sizeof(s_tcp_server_ip) - 1U);
            s_tcp_server_ip[sizeof(s_tcp_server_ip) - 1U] = '\0';
        }
        if (cfg->tcp_server_port > 0U) {
            s_tcp_server_port = cfg->tcp_server_port;
        }
        if (cfg->report_period_ms >= 500U) {
            s_report_period_ms = cfg->report_period_ms;
        }
        BSP_UART1_Printf("[WiFi] Config from EEPROM: SSID=%s Server=%s:%u Period=%ums\r\n",
                         s_wifi_ssid, s_tcp_server_ip, (unsigned)s_tcp_server_port,
                         (unsigned)s_report_period_ms);
    }
#endif
}

/* ==========================================================================
 *  内部辅助函数
 * ========================================================================== */

/**
 * @brief  光照等级转字符串
 */
static const char *wifi_level_str(uint8_t level)
{
    switch (level) {
        case LIGHT_LEVEL_BRIGHT: return "BRIGHT";
        case LIGHT_LEVEL_MEDIUM: return "MEDIUM";
        case LIGHT_LEVEL_DARK:   return "DARK";
        default:                 return "UNKNOWN";
    }
}

/* ==========================================================================
 *  WiFi 远程控灯（TCP 服务器端发送指令，经ESP8266 +IPD 到达 MCU）
 * ========================================================================== */
#if APP_WIFI_LED_ENABLE && BSP_LED_ENABLE

#define WIFI_LED_POLL_INTERVAL_MS  100U   /*!< 远程指令轮询间隔（上报周期内分段检查，越小响应越快）*/

static protocol_parser_t s_wifi_cmd_parser;   /*!< WiFi 远程指令解析器（文本，*/
static proto_hex_parser_t s_wifi_hex_parser;  /*!< WiFi 远程指令解析器（十六进制）*/
static uint8_t           s_wifi_parser_inited;  /*!< 解析器是否已初始化*/

/**
 * @brief  指令ID -> TCP回显字符串 */
static const char *wifi_cmd_reply(protocol_cmd_t cmd)
{
    switch (cmd) {
        case PROTOCOL_CMD_LED1_ON:     return "LED1 ON\r\n";
        case PROTOCOL_CMD_LED1_OFF:    return "LED1 OFF\r\n";
        case PROTOCOL_CMD_LED2_ON:     return "LED2 ON\r\n";
        case PROTOCOL_CMD_LED2_OFF:    return "LED2 OFF\r\n";
        case PROTOCOL_CMD_LED_ALL_ON:  return "ALL ON\r\n";
        case PROTOCOL_CMD_LED_ALL_OFF: return "ALL OFF\r\n";
        case PROTOCOL_CMD_HELP:        return "CMDS: LED1ON LED1OFF LED2ON LED2OFF ALLON ALLOFF HELP\r\n";
        default:                       return NULL;
    }
}

/**
 * @brief  执行 LED 动作并通过 TCP 回显结果
 */
static void wifi_exec_and_reply(protocol_cmd_t cmd)
{
    const char *reply;

    APP_CMD_Exec(cmd);   /* 执行 LED 动作 */

    reply = wifi_cmd_reply(cmd);
    if (reply != NULL) {
        (void)BSP_ESP8266_TCPSend((const uint8_t *)reply,
                                  (uint32_t)strlen(reply));
    }
}

/**
 * @brief  轮询 TCP 接收数据，解析远程指令并执行 LED 动作 + TCP 回显
 * @note   应在已连接TCP 的状态下频繁调用；非阻塞，无数据时立即返回。
 *         无换行符指令也能执行：PollIPD 取完当前所有数据后立即强制成帧。 */
static void wifi_handle_remote_cmd(void)
{
    uint8_t  ipd_buf[64];
    uint32_t n;
    uint32_t i;

    if (s_wifi_parser_inited == 0U) {
        Protocol_Init(&s_wifi_cmd_parser);
        ProtoHex_Init(&s_wifi_hex_parser);
        s_wifi_parser_inited = 1U;
    }

    /* 从 ESP8266 +IPD 缓冲提取 TCP 接收数据（可能多次到达，循环取出） */
    for (;;) {
        n = BSP_ESP8266_PollIPD(ipd_buf, sizeof(ipd_buf));
        if (n == 0U) {
            break;
        }

        /* [DEBUG] 打印收到的原始数据 */
        {
            uint32_t j;
            BSP_UART1_Printf("[WiFi-CMD] got %u bytes:", (unsigned)n);
            for (j = 0U; j < n; j++) {
                if ((ipd_buf[j] >= 0x20U) && (ipd_buf[j] < 0x7FU)) {
                    BSP_UART1_Printf("%c", ipd_buf[j]);
                } else {
                    BSP_UART1_Printf("\\x%02X", ipd_buf[j]);
                }
            }
            BSP_UART1_Printf("\r\n");
        }

        for (i = 0U; i < n; i++) {
            /* ---- 十六进制协议解析（优先，固定8字节帧） ---- */
            uint8_t hex_frame[PROTO_HEX_FRAME_SIZE];
            proto_hex_ret_t hret = ProtoHex_Feed(&s_wifi_hex_parser, ipd_buf[i], hex_frame);
            if (hret == PROTO_HEX_RET_FRAME) {
                uint8_t reply[PROTO_HEX_FRAME_SIZE];
                BSP_UART1_Printf("[WiFi-CMD] hex frame, cmd=0x%02X reg=0x%04X data=0x%04X\r\n",
                                 hex_frame[1],
                                 (unsigned)(((uint16_t)hex_frame[2] << 8) | hex_frame[3]),
                                 (unsigned)(((uint16_t)hex_frame[4] << 8) | hex_frame[5]));
                if (APP_HexCmd_Process(hex_frame, reply) != 0U) {
                    (void)BSP_ESP8266_TCPSend(reply, PROTO_HEX_FRAME_SIZE);
                }
                continue;   /* 十六进制帧不喂入文本解析器 */
            } else if (hret == PROTO_HEX_RET_CRC_ERROR) {
                BSP_UART1_Printf("[WiFi-CMD] hex frame CRC error\r\n");
                continue;
            }
            /* 正在接收十六进制帧时(len>0)，该字节属于十六进制帧，不喂文本解析器；
               len==0 说明该字节不是十六进制帧头，交给文本协议处理 */
            if (s_wifi_hex_parser.len > 0U) {
                continue;
            }

            /* ---- 文本协议解析 ---- */
            protocol_cmd_t cmd = PROTOCOL_CMD_NONE;
            protocol_ret_t ret = Protocol_Feed(&s_wifi_cmd_parser, ipd_buf[i], &cmd);

            if (ret == PROTOCOL_RET_FRAME) {
                BSP_UART1_Printf("[WiFi-CMD] frame matched, cmd=%d\r\n", (int)cmd);
                wifi_exec_and_reply(cmd);
            }
            /* 未知指令/超长：协议层静默丢弃，不回显（避免TCP风暴）*/
        }
    }

    /* 立即强制成帧：PollIPD 已把当前所有已到达的完整+IPD 数据取出并喂入解析器（
     * 此时解析器中残留的未完成帧就是完整指令（仅缺换行符），无需再等30ms。
     * 这样无换行符的指令在本次调用内即可执行，响应延迟 ≤ 轮询间隔(100ms)。*/
    if (s_wifi_cmd_parser.len > 0U) {
        protocol_cmd_t cmd = PROTOCOL_CMD_NONE;
        protocol_ret_t ret = Protocol_Feed(&s_wifi_cmd_parser, (uint8_t)'\n', &cmd);
        BSP_UART1_Printf("[WiFi-CMD] force-frame, len=%u, ret=%d, cmd=%d\r\n",
                         (unsigned)s_wifi_cmd_parser.len, (int)ret, (int)cmd);
        if (ret == PROTOCOL_RET_FRAME) {
            wifi_exec_and_reply(cmd);
        }
        /* 未知指令不回显*/
    }
}
#endif /* APP_WIFI_LED_ENABLE && BSP_LED_ENABLE */

/**
 * @brief  完整初始化链路：AT 测试 → 设模式 → 连 WiFi → 连 TCP
 * @retval 1=全部成功，0=失败（调用方应延迟后重试） */
static uint8_t wifi_full_init(void)
{
    ESP8266_Status_t st;
    char ip_buf[24];
    uint8_t at_fail_count = 0U;

    /* ── 1. AT 测试（模块在线？）── */
    BSP_UART1_Printf("[WiFi] AT test...\r\n");
    while (1) {
        st = BSP_ESP8266_TestAT(2000U);
        if (st == ESP8266_OK) {
            break;
        }
        at_fail_count++;
        BSP_UART1_Printf("[WiFi] AT failed (st=%d), retry %u/%u\r\n",
                         (int)st, (unsigned)at_fail_count, (unsigned)WIFI_MAX_AT_RETRY);
        if (at_fail_count >= WIFI_MAX_AT_RETRY) {
            BSP_UART1_Printf("[WiFi] AT failed too many times, reset module...\r\n");
            (void)BSP_ESP8266_Reset();
            at_fail_count = 0U;
        }
        osDelay(1000U);
    }
    BSP_UART1_Printf("[WiFi] AT OK\r\n");

    /* ── 2. 设为 Station 模式 ── */
    BSP_UART1_Printf("[WiFi] Set STA mode...\r\n");
    st = BSP_ESP8266_SetMode(ESP8266_MODE_STA);
    if (st != ESP8266_OK) {
        BSP_UART1_Printf("[WiFi] Set mode FAILED (st=%d)\r\n", (int)st);
        return 0U;
    }
    BSP_UART1_Printf("[WiFi] STA mode set\r\n");

    /* ── 3. 连接 WiFi 路由器 ── */
    BSP_UART1_Printf("[WiFi] Joining AP \"%s\" ...\r\n", s_wifi_ssid);
    st = BSP_ESP8266_JoinAP(s_wifi_ssid, s_wifi_password);
    if (st != ESP8266_OK) {
        BSP_UART1_Printf("[WiFi] Join AP FAILED (st=%d). Check SSID/password/2.4GHz\r\n", (int)st);
        return 0U;
    }
    BSP_UART1_Printf("[WiFi] WiFi connected\r\n");
    s_wifi_status |= 0x01U;   /* 标记 WiFi 已连接 */
    osDelay(500U);  /* 等待 DHCP 完全就绪，避免紧接着 CIFSR 超时 */

    /* ── 4. 查询模块 IP（调试信息） ── */
    st = BSP_ESP8266_GetIP(ip_buf, sizeof(ip_buf));
    if (st == ESP8266_OK) {
        BSP_UART1_Printf("[WiFi] Module IP: %s\r\n", ip_buf);
    } else {
        BSP_UART1_Printf("[WiFi] Get IP failed (st=%d), continue...\r\n", (int)st);
    }

    /* ── 5. 连接 TCP 服务器 ── */
    BSP_UART1_Printf("[WiFi] Connecting TCP %s:%u ...\r\n", s_tcp_server_ip, (unsigned)s_tcp_server_port);
    st = BSP_ESP8266_TCPConnect(s_tcp_server_ip, s_tcp_server_port);
    if (st != ESP8266_OK) {
        BSP_UART1_Printf("[WiFi] TCP connect FAILED (st=%d). Check server IP/port/firewall\r\n", (int)st);
        return 0U;
    }
    BSP_UART1_Printf("[WiFi] TCP connected, ready to report\r\n");
    s_wifi_status |= 0x02U;   /* 标记 TCP 已连接 */

    return 1U;
}

/**
 * @brief  读取 DHT11 + 光敏快照，格式化为上报字符串并通过 TCP 发送
 * @retval 1 发送成功；0 失败
 */
static uint8_t wifi_report_once(void)
{
    DHT11_Snapshot_t dht;
    LightSensor_Snapshot_t light;
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
    HC_SR04_Snapshot_t us;
#endif
    char payload[128];
    int  len;
    ESP8266_Status_t st;

    APP_DHT11_GetSnapshot(&dht);
    APP_LightSensor_GetSnapshot(&light);
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
    APP_HC_SR04_GetSnapshot(&us);
#endif

    /* 组装上报字符串：温湿度 + 光照 + 超声波距离 */
    if ((dht.valid == 0U) && (light.valid == 0U)) {
        /* 两个传感器都尚未采集到有效数据 */
        len = snprintf(payload, sizeof(payload),
                       "[SENSOR] warming up, no valid data yet\r\n");
    } else {
        len = snprintf(payload, sizeof(payload),
                       "[SENSOR] Temp=%u.%uC | Hum=%u.%u%% | Light=%u/%umV/%s",
                       (unsigned)dht.temperature_int, (unsigned)dht.temperature_dec,
                       (unsigned)dht.humidity_int,    (unsigned)dht.humidity_dec,
                       (unsigned)light.adc_value,     (unsigned)light.voltage_mv,
                       wifi_level_str(light.level));
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
        /* 追加超声波距离（有效时显示，无效时显示"--"*/
        if (us.valid) {
            len += snprintf(payload + len, sizeof(payload) - (size_t)len,
                            " | Dist=%umm", (unsigned)us.distance_mm);
        } else {
            len += snprintf(payload + len, sizeof(payload) - (size_t)len,
                            " | Dist=---");
        }
#endif
        len += snprintf(payload + len, sizeof(payload) - (size_t)len, "\r\n");
    }

    if (len <= 0) {
        return 0U;
    }
    if ((uint32_t)len >= sizeof(payload)) {
        len = (int)sizeof(payload) - 1;
    }

    st = BSP_ESP8266_TCPSend((const uint8_t *)payload, (uint32_t)len);
    if (st != ESP8266_OK) {
        BSP_UART1_Printf("[WiFi] TCPSend FAILED (st=%d)\r\n", (int)st);
        return 0U;
    }

    return 1U;
}

/* ==========================================================================
 *  任务入口
 * ========================================================================== */

void APP_WIFI_Task(void *argument)
{
    (void)argument;
    uint8_t connected = 0U;

    /* 从 EEPROM 加载 WiFi 配置（失败则用宏默认值） */
    wifi_load_config();

    BSP_UART1_Printf("[WiFi] Task started (report every %ums, server=%s:%u)\r\n",
                     (unsigned)s_report_period_ms, s_tcp_server_ip, (unsigned)s_tcp_server_port);

    /* 等待传感器任务先跑起来（延迟确保快照就绪）*/
    osDelay(500U);

    for (;;) {
        /* ── 阶段 A：未连接 → 执行完整初始化 ── */
        if (connected == 0U) {
            s_wifi_status = 0U;   /* 清除连接状态 */
            BSP_UART1_Printf("[WiFi] ===== Initializing ESP8266 =====\r\n");
            if (wifi_full_init() != 0U) {
                connected = 1U;
                BSP_UART1_Printf("[WiFi] ===== Init done, start reporting =====\r\n");
            } else {
                BSP_UART1_Printf("[WiFi] Init failed, retry in %ums...\r\n",
                                 (unsigned)WIFI_RETRY_DELAY_MS);
                osDelay(WIFI_RETRY_DELAY_MS);
                continue;
            }
        }

        /* ── 阶段 B：已连接 → 检查远程指令 + 周期上报 ── */
#if APP_WIFI_LED_ENABLE && BSP_LED_ENABLE
        wifi_handle_remote_cmd();   /* 上报前先处理积压的远程控灯指令 */
#endif

        if (wifi_report_once() == 0U) {
            /* 发送失败，标记断开，下一轮重新初始化 */
            connected = 0U;
            BSP_UART1_Printf("[WiFi] Report failed, will reconnect...\r\n");
            osDelay(WIFI_RETRY_DELAY_MS);
        } else {
            /* 上报周期内分段等待，每100ms 检查一次远程指令（控灯响应延迟 ≤100ms）*/
#if APP_WIFI_LED_ENABLE && BSP_LED_ENABLE
            uint32_t waited = 0U;
            while (waited < s_report_period_ms) {
                osDelay(WIFI_LED_POLL_INTERVAL_MS);
                waited += WIFI_LED_POLL_INTERVAL_MS;
                wifi_handle_remote_cmd();
            }
#else
            osDelay(s_report_period_ms);
#endif
        }
    }
}

#endif /* APP_WIFI_ENABLE && APP_TASKS_ENABLE && BSP_ESP8266_ENABLE */
