/**
 ******************************************************************************
 * @file    app_bt24.c
 * @brief   DX-BT24 蓝牙透传应用实现 — USART2 透传 + JSON 命令解析
 *
 *  数据通路：
 *    BT24 RX(ISR) -> 软件环形缓冲 -> [本任务] -> 行缓冲 -> 命令解析/状态识别
 *    小程序命令    -> BT24 -> USART2 -> 行缓冲 -> bt24_handle_command() 执行
 *    传感器快照    -> bt24_send_telemetry() -> USART2 -> BT24 -> 小程序
 *
 *  连接状态判定（AT+NOTI1 通知上位机连接状态）：
 *    - 收到 "+CONN"   -> 手机已连接（透传模式），可主动上报
 *    - 收到 "+DISC"   -> 手机断开（回到 AT 命令模式），停止主动上报
 *    - 收到合法 JSON  -> 必然是透传模式下的小程序指令，视为已连接
 *
 *  兼容性：
 *    - 所有业务动作复用 APP_Relay_Control / BSP_LED_* / BSP_BEEP_Beep，
 *      与 MQTT 云通道、矩阵键盘完全一致，多通道状态统一
 *    - 遥测数据源为 APP_DIAG_GetSnapshot（浮点快照），与 OLED/MQTT 同源
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_BT24_ENABLE

#include "app_bt24.h"
#include "bsp_bt24.h"
#include "cmsis_os.h"
#include "main.h"   /* HAL_GetTick 等 */
#include <stdio.h>
#include <stdlib.h> /* atoi */
#include <string.h>

#if APP_NETCFG_ENABLE
#include "app_netcfg.h"
#endif

#if APP_WATCHDOG_ENABLE
#include "app_watchdog.h"
#endif
#if APP_DIAG_ENABLE
#include "app_diag.h"
#endif
#if APP_RELAY_ENABLE
#include "app_relay.h"
#endif
#if APP_FAULT_ENABLE
#include "app_fault.h"
#endif
#if APP_ALARM_ENABLE
#include "app_alarm.h"
#endif
#if APP_CONFIG_ENABLE
#include "app_config.h"
#endif
#if BSP_LED_ENABLE
#include "bsp_led.h"
#endif
#if BSP_BEEP_ENABLE
#include "bsp_beep.h"
#endif
#if BSP_UART1_ENABLE
#include "bsp_uart.h"

/* 模块调试打印（UART1），受开关控制 */
#if APP_BT24_UART1_PRINTF_ENABLE && BSP_UART1_ENABLE
#define BT24_Printf(fmt, ...)  BSP_UART1_Printf(fmt, ##__VA_ARGS__)
#else
#define BT24_Printf(fmt, ...)  ((void)0)
#endif
#else
#define BT24_Printf(fmt, ...)  ((void)0)
#endif

/* ==========================================================================
 *  参数配置
 * ========================================================================== */
#define BT24_LINE_BUF_SIZE      256   /*!< 遥测帧缓冲区（一帧 JSON） */
#define BT24_RX_LINE_SIZE       1024  /*!< 行缓冲：需容纳 GETCFG/SETCFG 大帧（WiFi+云全字段） */
#define BT24_REPORT_PERIOD_MS   3000U /*!< 连接后主动上报周期（ms） */
#define BT24_AT_RETRY           3     /*!< AT+NOTI1 重试次数 */
#define BT24_AT_RETRY_DELAY_MS  1200U /*!< AT+NOTI1 重试间隔（ms） */
#define BT24_BOOT_DELAY_MS      500U  /*!< 任务启动后等待模块上电稳定（ms） */

/* ==========================================================================
 *  模块状态
 * ========================================================================== */
static volatile uint8_t s_bt24_connected;    /*!< 1=透传模式（手机已连接） */
static volatile uint8_t s_bt24_noti_ack;     /*!< 1=已确认 AT+NOTI1 生效 */

static char      s_line[BT24_RX_LINE_SIZE]; /*!< 行缓冲（RX，可容纳大帧） */
static uint32_t  s_line_len;
static uint32_t  s_last_report_ms;

/* ==========================================================================
 *  轻量 JSON 解析（仅需 cmd/val/mask/index/state/ms 字段）
 * ========================================================================== */

/**
 * @brief  从 JSON 提取字符串值：{"cmd":"RELAY"} -> "RELAY"
 * @retval 1=成功, 0=未找到
 */
static uint8_t bt24_json_get_str(const char *json, const char *key,
                                 char *out, uint32_t out_size)
{
    char search[32];
    const char *p;
    const char *start;
    const char *end;
    uint32_t len;

    if ((json == NULL) || (key == NULL) || (out == NULL) || (out_size == 0U)) {
        return 0U;
    }
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
    len = (uint32_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1U;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1U;
}

/**
 * @brief  从 JSON 提取整数值：{"val":1} -> 1
 * @retval 1=成功, 0=未找到
 */
static uint8_t bt24_json_get_int(const char *json, const char *key, int *value)
{
    char search[32];
    const char *p;
    const char *start;

    if ((json == NULL) || (key == NULL) || (value == NULL)) {
        return 0U;
    }
    snprintf(search, sizeof(search), "\"%s\":", key);
    p = strstr(json, search);
    if (p == NULL) {
        return 0U;
    }
    start = p + strlen(search);
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    *value = atoi(start);
    return 1U;
}

/**
 * @brief  从 JSON 提取浮点值：{"temp":60.5} -> 60.5（阈值设置用）
 * @retval 1=成功, 0=未找到
 */
static uint8_t bt24_json_get_float(const char *json, const char *key, float *value)
{
    char search[32];
    const char *p;
    const char *start;

    if ((json == NULL) || (key == NULL) || (value == NULL)) {
        return 0U;
    }
    snprintf(search, sizeof(search), "\"%s\":", key);
    p = strstr(json, search);
    if (p == NULL) {
        return 0U;
    }
    start = p + strlen(search);
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    *value = strtof(start, NULL);
    return 1U;
}

/**
 * @brief  JSON 字符串转义（" -> \"，\ -> \\），用于透传诊断描述
 */
static void bt24_json_escape(const char *src, char *dst, uint32_t dst_size)
{
    uint32_t di = 0U;
    if ((src == NULL) || (dst == NULL) || (dst_size == 0U)) {
        return;
    }
    while ((*src != '\0') && (di < (dst_size - 1U))) {
        if ((*src == '"') || (*src == '\\')) {
            if (di < (dst_size - 2U)) {
                dst[di++] = '\\';
            }
        }
        dst[di++] = *src;
        src++;
    }
    dst[di] = '\0';
}

/* ==========================================================================
 *  浮点转 int/dec（处理负数，dec 恒为正）
 * ========================================================================== */
static long bt24_pow10i(int n)
{
    long r = 1L;
    while (n-- > 0) {
        r *= 10L;
    }
    return r;
}

static void bt24_f2dec(float v, int dp_len, int *ip, long *dp)
{
    float sign = (v < 0.0f) ? -1.0f : 1.0f;
    float av   = v * sign;
    long  mul  = bt24_pow10i(dp_len);
    int   i    = (int)av;
    long  d    = (long)((av - (float)i) * (float)mul + 0.5f);

    if (d >= mul) {   /* 小数进位 */
        d = 0L;
        i++;
    }
    *ip = (int)((float)i * sign);
    *dp = d;
}

/* ==========================================================================
 *  遥测上报
 * ========================================================================== */

/**
 * @brief  发送一帧遥测 JSON 到透传通道（行尾 \r\n）
 * @note   - INA226 读取失败时（ina_valid=0）i/u/p 发送 null 而非 0.000，
 *            便于区分"传感器读数失败"与"电机真实断电/0电流"
 *          - 告警详情：al=等级(0无/1注意/2异常)，as=告警源位图(bit0温度/bit1振动/bit2电流)
 *          - 诊断详情：diag=状态(0正常/1注意/2异常)，mask=故障位图，
 *            conf=置信度%，dmsg=固件诊断描述（仅非正常时发送，已做JSON转义）
 */
static void bt24_send_telemetry(void)
{
    char buf[BT24_LINE_BUF_SIZE];
    char dmsg_buf[96];
    int  t_i = 0, h_i = 0, v_i = 0, c_i = 0, u_i = 0, p_i = 0;
    long t_d = 0, h_d = 0, v_d = 0, c_d = 0, u_d = 0, p_d = 0;
    uint8_t relay = 0U, alarm = 0U, diag_st = 0U, mask = 0U;
    uint8_t ina_ok = 0U;   /* INA226 数据有效标志 */
    uint8_t al = 0U, as = 0U, conf = 0U;
    uint8_t led_mask = 0U; /* LED状态位图（bit0=LED1, bit1=LED2，供小程序显示真实灯态） */
    const char *dmsg_open  = "";
    const char *dmsg_body  = "";
    const char *dmsg_close = "";

    dmsg_buf[0] = '\0';

#if APP_DIAG_ENABLE
    APP_Diag_Snapshot_t snap;
    APP_DIAG_GetSnapshot(&snap);
    bt24_f2dec(snap.temperature, 1, &t_i, &t_d);
    bt24_f2dec(snap.humidity,    1, &h_i, &h_d);
    bt24_f2dec(snap.vibration,   2, &v_i, &v_d);
    ina_ok = snap.ina_valid;
    if (ina_ok != 0U) {
        bt24_f2dec(snap.current, 3, &c_i, &c_d);
        bt24_f2dec(snap.voltage, 3, &u_i, &u_d);
        bt24_f2dec(snap.power,   3, &p_i, &p_d);
    }
#if APP_ALARM_ENABLE
    {
        AlarmStatus_t st;
        APP_ALARM_GetStatus(&st);
        alarm = st.active;
        al    = st.level;
        as    = st.source_mask;
    }
#endif
#endif /* APP_DIAG_ENABLE */

#if APP_RELAY_ENABLE
    relay = APP_Relay_GetState();
#endif

#if BSP_LED_ENABLE
    if (BSP_LED_GetState(BSP_LED1) != 0U) { led_mask |= 0x01U; }
    if (BSP_LED_GetState(BSP_LED2) != 0U) { led_mask |= 0x02U; }
#endif

#if APP_FAULT_ENABLE
    {
        FaultDiagnosis_t dg;
        APP_FAULT_GetResult(&dg);
        diag_st = (uint8_t)dg.status;
        mask    = (uint8_t)dg.fault_mask;
        conf    = (uint8_t)(dg.confidence * 100.0f);
        if (dg.status != APP_FAULT_NORMAL) {
            bt24_json_escape(dg.description, dmsg_buf, sizeof(dmsg_buf));
            dmsg_open  = ",\"dmsg\":\"";
            dmsg_body  = dmsg_buf;
            dmsg_close = "\"";
        }
    }
#endif

    if (ina_ok != 0U) {
        snprintf(buf, sizeof(buf),
                 "{\"t\":%d.%ld,\"h\":%d.%ld,\"v\":%d.%02ld,"
                 "\"i\":%d.%03ld,\"u\":%d.%03ld,\"p\":%d.%03ld,"
                 "\"relay\":%u,\"alarm\":%u,\"al\":%u,\"as\":%u,"
                 "\"diag\":%u,\"mask\":%u,\"l\":%u,\"conf\":%u%s%s%s}\r\n",
                 t_i, t_d, h_i, h_d, v_i, v_d,
                 c_i, c_d, u_i, u_d, p_i, p_d,
                 (unsigned)relay, (unsigned)alarm, (unsigned)al, (unsigned)as,
                 (unsigned)diag_st, (unsigned)mask, (unsigned)led_mask, (unsigned)conf,
                 dmsg_open, dmsg_body, dmsg_close);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"t\":%d.%ld,\"h\":%d.%ld,\"v\":%d.%02ld,"
                 "\"i\":null,\"u\":null,\"p\":null,"
                 "\"relay\":%u,\"alarm\":%u,\"al\":%u,\"as\":%u,"
                 "\"diag\":%u,\"mask\":%u,\"l\":%u,\"conf\":%u%s%s%s}\r\n",
                 t_i, t_d, h_i, h_d, v_i, v_d,
                 (unsigned)relay, (unsigned)alarm, (unsigned)al, (unsigned)as,
                 (unsigned)diag_st, (unsigned)mask, (unsigned)led_mask, (unsigned)conf,
                 dmsg_open, dmsg_body, dmsg_close);
    }
    BSP_BT24_SendString(buf);
    BT24_Printf("[BT24] TX telemetry (ina_ok=%u, al=%u, as=0x%02X, diag=%u)\r\n",
                (unsigned)ina_ok, (unsigned)al, (unsigned)as, (unsigned)diag_st);
}

/* ==========================================================================
 *  命令执行（与 MQTT 云通道/矩阵键盘共用同一业务入口）
 * ========================================================================== */

/**
 * @brief  通用应答帧
 */
static void bt24_reply(uint8_t ok, const char *cmd)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":%u,\"cmd\":\"%s\"}\r\n", ok ? 1U : 0U, cmd);
    BSP_BT24_SendString(buf);
}

/**
 * @brief  回发当前阈值（GETTH 查询 / SETTH 保存后确认，1位小数）
 */
static void bt24_reply_thresholds(const char *cmd)
{
    char buf[96];
#if APP_CONFIG_ENABLE
    int  t_i = 0, v_i = 0, c_i = 0;
    long t_d = 0, v_d = 0, c_d = 0;
    bt24_f2dec(g_threshold.temp_high, 1, &t_i, &t_d);
    bt24_f2dec(g_threshold.vib_high,  1, &v_i, &v_d);
    bt24_f2dec(g_threshold.curr_high, 1, &c_i, &c_d);
    snprintf(buf, sizeof(buf),
             "{\"ok\":1,\"cmd\":\"%s\",\"temp\":%d.%ld,\"vib\":%d.%ld,\"curr\":%d.%ld}\r\n",
             cmd, t_i, t_d, v_i, v_d, c_i, c_d);
#else
    snprintf(buf, sizeof(buf), "{\"ok\":0,\"cmd\":\"%s\"}\r\n", cmd);
#endif
    BSP_BT24_SendString(buf);
}

/* ==========================================================================
 *  网络配置（GETCFG / SETCFG）— 依赖 app_netcfg（内部Flash Sector7）
 *  协议：
 *    查询: {"cmd":"GETCFG"}
 *    应答: {"ok":1,"cmd":"GETCFG","wifi_ssid":"...","wifi_password":"...",
 *           "broker_host":"...","broker_ip":"...","broker_port":1883,
 *           "client_id":"...","username":"...","password":"...","device_id":"..."}
 *    修改: {"cmd":"SETCFG","wifi_ssid":"...","wifi_password":"...",...}
 *          （缺省字段保留当前值；端口 1~65535；字符串禁止含 " 和 \）
 *    应答: {"ok":1,"cmd":"SETCFG"} / {"ok":0,"cmd":"SETCFG"}
 *  保存成功后 MQTT 任务自动按新配置重连（无需重启设备）。
 * ========================================================================== */
#if APP_NETCFG_ENABLE

#define BT24_CFG_REPLY_SIZE  1024   /*!< GETCFG 应答缓冲（全字段 JSON） */

/**
 * @brief  追加 JSON 转义字符串（" -> \"，\ -> \\），返回写入字符数
 */
static int bt24_append_escaped(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t di = 0U;
    if ((dst == NULL) || (dst_size == 0U)) {
        return 0;
    }
    while ((*src != '\0') && (di < (dst_size - 1U))) {
        if ((*src == '"') || (*src == '\\')) {
            if (di < (dst_size - 2U)) {
                dst[di++] = '\\';
            }
        }
        dst[di++] = *src;
        src++;
    }
    dst[di] = '\0';
    return (int)di;
}

/**
 * @brief  回发当前网络配置（GETCFG）
 * @note   拼接全程带界保护：snprintf 返回"应写长度"，超界时停止拼接，
 *         避免字符串全量转义（" \ 翻倍）时越界写
 */
static void bt24_reply_netcfg(const char *cmd)
{
    static char buf[BT24_CFG_REPLY_SIZE];
    int len = 0;
    int cap = (int)sizeof(buf);

#define NETCFG_APPEND_RAW(fmt, ...)                                        \
    do {                                                                   \
        if (len < cap) {                                                   \
            int n = snprintf(buf + len, (size_t)(cap - len), fmt, ##__VA_ARGS__); \
            if (n > 0) len += n;                                           \
        }                                                                  \
    } while (0)
#define NETCFG_APPEND_STR(s)                                               \
    do {                                                                   \
        if (len < cap) {                                                   \
            len += bt24_append_escaped(buf + len, (uint32_t)(cap - len), (s)); \
        }                                                                  \
    } while (0)

    NETCFG_APPEND_RAW("{\"ok\":1,\"cmd\":\"%s\",\"wifi_ssid\":\"", cmd);
    NETCFG_APPEND_STR(g_netcfg.wifi_ssid);
    NETCFG_APPEND_RAW("\",\"wifi_password\":\"");
    NETCFG_APPEND_STR(g_netcfg.wifi_password);
    NETCFG_APPEND_RAW("\",\"broker_host\":\"");
    NETCFG_APPEND_STR(g_netcfg.broker_host);
    NETCFG_APPEND_RAW("\",\"broker_ip\":\"");
    NETCFG_APPEND_STR(g_netcfg.broker_ip);
    NETCFG_APPEND_RAW("\",\"broker_port\":%u,\"client_id\":\"",
                      (unsigned)g_netcfg.broker_port);
    NETCFG_APPEND_STR(g_netcfg.client_id);
    NETCFG_APPEND_RAW("\",\"username\":\"");
    NETCFG_APPEND_STR(g_netcfg.username);
    NETCFG_APPEND_RAW("\",\"password\":\"");
    NETCFG_APPEND_STR(g_netcfg.password);
    NETCFG_APPEND_RAW("\",\"device_id\":\"");
    NETCFG_APPEND_STR(g_netcfg.device_id);
    NETCFG_APPEND_RAW("\"}\r\n");

#undef NETCFG_APPEND_RAW
#undef NETCFG_APPEND_STR

    BSP_BT24_SendString(buf);
    BT24_Printf("[BT24] GETCFG sent (len=%d)\r\n", len);
}

/**
 * @brief  处理 SETCFG：解析可选字段 → 校验 → 提交到 app_netcfg（写Flash）
 * @note   Flash 128KB 扇区擦除约 1~2s，写入前先喂看门狗，避免 IWDG 复位
 */
static void bt24_handle_setcfg(const char *line)
{
    NetConfig_t tmp;
    int v;
    uint8_t changed = 0U;
    uint8_t ret;
    char reply[96];

    /* 未下发的字段保留当前值 */
    memcpy(&tmp, &g_netcfg, sizeof(tmp));

    if (bt24_json_get_str(line, "wifi_ssid", tmp.wifi_ssid, sizeof(tmp.wifi_ssid))) {
        if (tmp.wifi_ssid[0] == '\0') {
            bt24_reply(0U, "SETCFG");
            return;
        }
        changed = 1U;
    }
    if (bt24_json_get_str(line, "wifi_password", tmp.wifi_password, sizeof(tmp.wifi_password))) {
        changed = 1U;
    }
    if (bt24_json_get_str(line, "broker_host", tmp.broker_host, sizeof(tmp.broker_host))) {
        changed = 1U;
    }
    if (bt24_json_get_str(line, "broker_ip", tmp.broker_ip, sizeof(tmp.broker_ip))) {
        changed = 1U;
    }
    if (bt24_json_get_int(line, "broker_port", &v)) {
        if ((v < 1) || (v > 65535)) {
            bt24_reply(0U, "SETCFG");
            return;
        }
        tmp.broker_port = (uint16_t)v;
        changed = 1U;
    }
    if (bt24_json_get_str(line, "client_id", tmp.client_id, sizeof(tmp.client_id))) {
        changed = 1U;
    }
    if (bt24_json_get_str(line, "username", tmp.username, sizeof(tmp.username))) {
        changed = 1U;
    }
    if (bt24_json_get_str(line, "password", tmp.password, sizeof(tmp.password))) {
        changed = 1U;
    }
    if (bt24_json_get_str(line, "device_id", tmp.device_id, sizeof(tmp.device_id))) {
        changed = 1U;
    }

    if (changed == 0U) {
        bt24_reply(0U, "SETCFG");   /* 没有任何可修改字段 */
        return;
    }
    /* 至少保留一个 Broker 地址（域名或IP） */
    if ((tmp.broker_host[0] == '\0') && (tmp.broker_ip[0] == '\0')) {
        bt24_reply(0U, "SETCFG");
        return;
    }

#if APP_WATCHDOG_ENABLE
    Watchdog_Kick(WDT_TASK_BT24);   /* 写Flash前喂狗（擦除约1~2s） */
#endif
    ret = APP_NetCfg_Apply(&tmp);

    snprintf(reply, sizeof(reply),
             "{\"ok\":%u,\"cmd\":\"SETCFG\",\"saved\":%u}\r\n",
             (ret == 0U) ? 1U : 0U, (ret == 0U) ? 1U : 0U);
    BSP_BT24_SendString(reply);
    BT24_Printf("[BT24] SETCFG applied (saved=%u), MQTT will reconnect\r\n",
                (unsigned)((ret == 0U) ? 1U : 0U));
}

#endif /* APP_NETCFG_ENABLE */

/**
 * @brief  按位掩码控制 4 个 LED（bit0=LED1 ... bit3=LED4）
 */
static void bt24_exec_led_mask(int mask)
{
#if BSP_LED_ENABLE
    if (mask & 0x01) { BSP_LED_On(BSP_LED1);  } else { BSP_LED_Off(BSP_LED1); }
    if (mask & 0x02) { BSP_LED_On(BSP_LED2);  } else { BSP_LED_Off(BSP_LED2); }
    if (mask & 0x04) { BSP_LED_On(BSP_LED3);  } else { BSP_LED_Off(BSP_LED3); }
    if (mask & 0x08) { BSP_LED_On(BSP_LED4);  } else { BSP_LED_Off(BSP_LED4); }
#else
    (void)mask;
#endif
}

/**
 * @brief  按序号控制单个 LED（index 1~4, state 0/1）
 */
static void bt24_exec_led_single(int index, int state)
{
#if BSP_LED_ENABLE
    BSP_LED_t led;
    switch (index) {
        case 1: led = BSP_LED1; break;
        case 2: led = BSP_LED2; break;
        case 3: led = BSP_LED3; break;
        case 4: led = BSP_LED4; break;
        default: return;
    }
    if (state) {
        BSP_LED_On(led);
    } else {
        BSP_LED_Off(led);
    }
#else
    (void)index;
    (void)state;
#endif
}

/**
 * @brief  处理一行小程序 JSON 命令
 */
static void bt24_handle_command(const char *line)
{
    char cmd[16];
    int  val = 0, index = 0, state = 0, ms = 0;
    char buf[96];

    if (!bt24_json_get_str(line, "cmd", cmd, sizeof(cmd))) {
        bt24_reply(0U, "?");
        return;
    }

    if (strcmp(cmd, "QUERY") == 0) {
        bt24_send_telemetry();      /* 立即回一帧实时数据 */
        bt24_reply(1U, "QUERY");
    } else if (strcmp(cmd, "PING") == 0) {
        bt24_reply(1U, "PING");
    } else if (strcmp(cmd, "RELAY") == 0) {
        if (bt24_json_get_int(line, "val", &val)) {
#if APP_RELAY_ENABLE
            APP_Relay_Control((uint8_t)((val != 0) ? 1U : 0U));
            snprintf(buf, sizeof(buf), "{\"ok\":1,\"cmd\":\"RELAY\",\"val\":%d}\r\n",
                     (val != 0) ? 1 : 0);
            BSP_BT24_SendString(buf);
            BT24_Printf("[BT24] RELAY=%d\r\n", (val != 0) ? 1 : 0);
#else
            bt24_reply(0U, "RELAY");
            BT24_Printf("[BT24] RELAY disabled in build\r\n");
#endif
        } else {
            bt24_reply(0U, "RELAY");
        }
    } else if (strcmp(cmd, "LED") == 0) {
        if (bt24_json_get_int(line, "mask", &val)) {
            bt24_exec_led_mask(val);
            snprintf(buf, sizeof(buf), "{\"ok\":1,\"cmd\":\"LED\",\"mask\":%d}\r\n", val);
            BSP_BT24_SendString(buf);
            BT24_Printf("[BT24] LED mask=0x%X\r\n", val);
        } else if (bt24_json_get_int(line, "index", &index) &&
                   bt24_json_get_int(line, "state", &state)) {
            bt24_exec_led_single(index, state);
            snprintf(buf, sizeof(buf), "{\"ok\":1,\"cmd\":\"LED\",\"index\":%d,\"state\":%d}\r\n",
                     index, state);
            BSP_BT24_SendString(buf);
            BT24_Printf("[BT24] LED%d=%d\r\n", index, state);
        } else {
            bt24_reply(0U, "LED");
        }
    } else if (strcmp(cmd, "BEEP") == 0) {
        if (bt24_json_get_int(line, "ms", &ms)) {
            if (ms > 1000) { ms = 1000; }   /* 上限保护，避免阻塞过久 */
#if BSP_BEEP_ENABLE
            BSP_BEEP_Beep((uint32_t)ms);
#endif
            snprintf(buf, sizeof(buf), "{\"ok\":1,\"cmd\":\"BEEP\",\"ms\":%d}\r\n", ms);
            BSP_BT24_SendString(buf);
        } else {
            bt24_reply(0U, "BEEP");
        }
    } else if (strcmp(cmd, "GETTH") == 0) {
        /* 查询当前告警阈值（温度℃/振动g/电流A，1位小数） */
        bt24_reply_thresholds("GETTH");
        BT24_Printf("[BT24] GETTH\r\n");
    } else if (strcmp(cmd, "SETTH") == 0) {
        /* 修改告警阈值：{"cmd":"SETTH","temp":60.0,"vib":3.0,"curr":5.0}，
         * 缺省字段保留原值；校验后写入 g_threshold 并保存到内部Flash。
         * 注意：32位对齐 float 写为原子操作，与按键任务共用同一全局阈值 */
#if APP_CONFIG_ENABLE
        {
            float v;
            uint8_t changed = 0U;
            if (bt24_json_get_float(line, "temp", &v)) {
                if ((v >= 0.0f) && (v <= 100.0f)) { g_threshold.temp_high = v; changed = 1U; }
            }
            if (bt24_json_get_float(line, "vib", &v)) {
                if ((v >= 0.0f) && (v <= 10.0f)) { g_threshold.vib_high = v; changed = 1U; }
            }
            if (bt24_json_get_float(line, "curr", &v)) {
                if ((v >= 0.0f) && (v <= 20.0f)) { g_threshold.curr_high = v; changed = 1U; }
            }
            if (changed != 0U) {
                if (APP_CONFIG_Save() != 0U) {
                    BT24_Printf("[BT24] SETTH save to flash FAILED\r\n");
                }
            }
            bt24_reply_thresholds("SETTH");
            BT24_Printf("[BT24] SETTH temp=%d.%ld vib=%d.%ld curr=%d.%ld\r\n",
                        (int)g_threshold.temp_high,
                        (long)((g_threshold.temp_high - (float)(int)g_threshold.temp_high) * 10.0f + 0.5f),
                        (int)g_threshold.vib_high,
                        (long)((g_threshold.vib_high - (float)(int)g_threshold.vib_high) * 10.0f + 0.5f),
                        (int)g_threshold.curr_high,
                        (long)((g_threshold.curr_high - (float)(int)g_threshold.curr_high) * 10.0f + 0.5f));
        }
#else
        bt24_reply(0U, "SETTH");
#endif
#if APP_NETCFG_ENABLE
    } else if (strcmp(cmd, "GETCFG") == 0) {
        /* 查询当前 WiFi/云配置（小程序"读取配置"） */
        bt24_reply_netcfg("GETCFG");
        BT24_Printf("[BT24] GETCFG\r\n");
    } else if (strcmp(cmd, "SETCFG") == 0) {
        /* 修改 WiFi/云配置（小程序"保存配置"），MQTT 自动按新配置重连 */
        bt24_handle_setcfg(line);
#endif
    } else {
        bt24_reply(0U, cmd);   /* 未知命令 */
    }
}

/* ==========================================================================
 *  行解析：区分 AT 响应 / 连接状态通知 / 小程序命令
 * ========================================================================== */
static void bt24_process_line(const char *line)
{
    if (line[0] == '\0') {
        return;
    }

    /* 1) 小程序 JSON 命令（透传模式下才能收到） */
    if (line[0] == '{') {
        s_bt24_connected = 1U;   /* 能收到命令 = 手机已连接 */
        bt24_handle_command(line);
        return;
    }

    /* 2) AT+NOTI1 生效确认：+NOTI=1 */
    if (strstr(line, "+NOTI") != NULL) {
        s_bt24_noti_ack = 1U;
        BT24_Printf("[BT24] NOTI ack\r\n");
        return;
    }

    /* 3) 连接状态通知：OK+CONN<mac> / OK+DISC */
    if (strstr(line, "CONN") != NULL) {
        s_bt24_connected = 1U;
        s_last_report_ms = 0U;   /* 连接瞬间立即补一帧 */
        BT24_Printf("[BT24] Phone connected\r\n");
        return;
    }
    if (strstr(line, "DISC") != NULL) {
        s_bt24_connected = 0U;
        s_bt24_noti_ack   = 0U;
        BT24_Printf("[BT24] Phone disconnected, re-arm AT+NOTI1\r\n");
        /* 回到 AT 命令模式：重新启用连接状态通知，供下次连接识别 */
        BSP_BT24_SendAT("AT+NOTI1");
        return;
    }

    /* 4) 其他 AT 响应（OK / ERROR / AT 回显等）忽略 */
}

/* ==========================================================================
 *  初始化与 AT 配置
 * ========================================================================== */

void APP_BT24_Init(void)
{
#if BSP_BT24_ENABLE
    BSP_BT24_Init();   /* 启动 USART2 DMA 循环接收 + IDLE 判帧 */
#endif
}

/**
 * @brief  任务启动后发送 AT+NOTI1（通知上位机连接状态），带重试
 * @note   模块上电约数百毫秒后才进入 AT 命令模式，故任务内延时后配置；
 *         若手机已先连接（透传模式），指令会被透传到手机，无害
 */
static void bt24_at_config(void)
{
    uint8_t i;
    for (i = 0U; i < BT24_AT_RETRY; i++) {
        if (s_bt24_noti_ack != 0U) {
            return;
        }
        BSP_BT24_SendAT("AT+NOTI1");
        BT24_Printf("[BT24] send AT+NOTI1 (#%u)\r\n", (unsigned)(i + 1U));
#if APP_WATCHDOG_ENABLE
        Watchdog_DelayWithKick(WDT_TASK_BT24, BT24_AT_RETRY_DELAY_MS);
#else
        osDelay(BT24_AT_RETRY_DELAY_MS);
#endif
    }
}

/* ==========================================================================
 *  任务主体
 * ========================================================================== */
void APP_BT24_Task(void *argument)
{
    uint8_t ch;
    (void)argument;

    /* 等待模块上电稳定 */
    osDelay(BT24_BOOT_DELAY_MS);

    /* 配置连接状态通知（未连接=AT命令模式，指令有效） */
    bt24_at_config();

    s_line_len = 0U;
    s_last_report_ms = 0U;

    for (;;) {
        /* ---- 泵取接收数据，按行拆帧 ---- */
        while (BSP_BT24_Available() > 0U) {
            (void)BSP_BT24_Read(&ch, 1U);
            if (ch == '\n') {
                s_line[s_line_len] = '\0';
                bt24_process_line(s_line);
                s_line_len = 0U;
            } else if (ch != '\r') {
                if (s_line_len < (BT24_RX_LINE_SIZE - 1U)) {
                    s_line[s_line_len++] = (char)ch;
                } else {
                    s_line_len = 0U;   /* 超长帧丢弃，防止越界 */
                }
            }
        }

        /* ---- 连接后周期上报遥测 ---- */
        if ((s_bt24_connected != 0U) &&
            ((HAL_GetTick() - s_last_report_ms) >= BT24_REPORT_PERIOD_MS)) {
            s_last_report_ms = HAL_GetTick();
            bt24_send_telemetry();
        }

#if APP_WATCHDOG_ENABLE
        Watchdog_Kick(WDT_TASK_BT24);
#endif
        osDelay(20U);
    }
}

#endif /* APP_BT24_ENABLE */
