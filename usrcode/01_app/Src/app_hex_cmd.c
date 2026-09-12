/**
 ******************************************************************************
 * @file    app_hex_cmd.c
 * @brief   十六进制协议指令执行层 — 寄存器读写 + LED 控制 + 配置修改 + EEPROM保存
 *
 *  本模块为公共执行层，供串口1、串口3、WiFi 远程控灯共用。
 *  输入一帧字节十六进制指令，输出字节应答帧。
 *  寄存器写策略：
 *    - 数值型寄存器（LED 状态、端口、周期、校准）：写后立即保存 EEPROM
 *    - 字符串型寄存器（SSID、密码）：连续写累积，遇到 0x0000 结束符时保存
 *    - TCP IP：写高6位暂存，写低16位时组合并保存
 *    - 恢复默认：写0xA5A5到REG_RESET_DEFAULT触发
 ******************************************************************************
 */
#include "module_cfg.h"
#if PROTOCOL_ENABLE

#include "app_hex_cmd.h"
#include "protocol_hex.h"
#include "app_eeprom.h"
#include "bsp_uart.h"
#include <string.h>

#if BSP_LED_ENABLE
#include "bsp_led.h"
#endif
#if APP_DHT11_ENABLE
#include "app_dht11.h"
#endif
#if APP_LIGHT_SENSOR_ENABLE
#include "app_light_sensor.h"
#endif
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
#include "app_hc_sr04.h"
#endif
#if APP_WIFI_ENABLE
#include "app_wifi.h"
#endif

/* ========== 字符串配置写缓冲（连续写寄存器累积） ========== */
static char s_ssid_buf[32];
static uint8_t s_ssid_writing;
static char s_pwd_buf[64];
static uint8_t s_pwd_writing;
static uint16_t s_tcp_ip_high;   /* TCP IP 高6位暂存 */

/* ========== 内部辅助函数 ========== */

/**
 * @brief  读取当前 LED 状态位域 */
static uint8_t read_led_state(void)
{
    uint8_t state = 0U;
#if BSP_LED_ENABLE
    if (BSP_LED_GetState(BSP_LED1)) state |= 0x01U;
    if (BSP_LED_GetState(BSP_LED2)) state |= 0x02U;
    if (BSP_LED_GetState(BSP_LED3)) state |= 0x04U;
    if (BSP_LED_GetState(BSP_LED4)) state |= 0x08U;
#endif
    return state;
}

/**
 * @brief  设置 LED 状态位域 */
static void write_led_state(uint8_t state)
{
#if BSP_LED_ENABLE
    if (state & 0x01U) BSP_LED_On(BSP_LED1); else BSP_LED_Off(BSP_LED1);
    if (state & 0x02U) BSP_LED_On(BSP_LED2); else BSP_LED_Off(BSP_LED2);
    if (state & 0x04U) BSP_LED_On(BSP_LED3); else BSP_LED_Off(BSP_LED3);
    if (state & 0x08U) BSP_LED_On(BSP_LED4); else BSP_LED_Off(BSP_LED4);
#endif
}

/**
 * @brief  读寄存器
 * @retval 0=成功(值在*data), 非0=非法地址
 */
static uint8_t reg_read(uint16_t reg, uint16_t *data)
{
    const app_config_t *cfg = APP_EEPROM_GetConfig();

    switch (reg) {
    case REG_DEVICE_ID:
        *data = 0x0001U;
        return 0U;
    case REG_FW_VERSION:
        *data = 0x0100U;
        return 0U;
    case REG_PROTO_VERSION:
        *data = 0x0100U;
        return 0U;
    case REG_RUN_STATUS:
        *data = 0U;
#if APP_WIFI_ENABLE
        *data |= APP_WIFI_GetStatus();
#endif
        return 0U;
    case REG_LED_STATE:
        *data = (uint16_t)read_led_state();
        return 0U;
    case REG_LED_ENABLE_MASK:
        *data = 0x000FU;
        return 0U;
#if APP_DHT11_ENABLE
    case REG_TEMPERATURE: {
        DHT11_Snapshot_t dht;
        APP_DHT11_GetSnapshot(&dht);
        *data = (uint16_t)(dht.temperature_int
 * 10 + dht.temperature_dec);
        return 0U;
    }
    case REG_HUMIDITY: {
        DHT11_Snapshot_t dht;
        APP_DHT11_GetSnapshot(&dht);
        *data = (uint16_t)(dht.humidity_int
 * 10 + dht.humidity_dec);
        return 0U;
    }
#endif
#if APP_LIGHT_SENSOR_ENABLE
    case REG_LIGHT_ADC: {
        LightSensor_Snapshot_t light;
        APP_LightSensor_GetSnapshot(&light);
        *data = light.adc_value;
        return 0U;
    }
    case REG_LIGHT_LEVEL: {
        LightSensor_Snapshot_t light;
        APP_LightSensor_GetSnapshot(&light);
        *data = (uint16_t)light.level;
        return 0U;
    }
#endif
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
    case REG_ULTRASONIC_DIST: {
        HC_SR04_Snapshot_t us;
        APP_HC_SR04_GetSnapshot(&us);
        *data = us.valid ? us.distance_mm : 0U;
        return 0U;
    }
#endif
    case REG_RUNTIME_LOW:
        *data = (uint16_t)(cfg->runtime_seconds & 0xFFFFU);
        return 0U;
    case REG_RUNTIME_HIGH:
        *data = (uint16_t)(cfg->runtime_seconds >> 16);
        return 0U;
    case REG_REBOOT_COUNT:
        *data = cfg->reboot_count;
        return 0U;
    /* WiFi SSID: 每寄存器 2 字符，大端 */
    case REG_WIFI_SSID_BASE ... (REG_WIFI_SSID_BASE + 15): {
        uint16_t idx = (reg - REG_WIFI_SSID_BASE) * 2U;
        uint8_t c0 = (idx < sizeof(cfg->wifi_ssid)) ? (uint8_t)cfg->wifi_ssid[idx] : 0U;
        uint8_t c1 = (idx + 1U < sizeof(cfg->wifi_ssid)) ? (uint8_t)cfg->wifi_ssid[idx + 1U] : 0U;
        *data = (uint16_t)((uint16_t)c0 << 8 | c1);
        return 0U;
    }
    /* WiFi密码: 每寄存器2字符 */
    case REG_WIFI_PWD_BASE ... (REG_WIFI_PWD_BASE + 31): {
        uint16_t idx = (reg - REG_WIFI_PWD_BASE) * 2U;
        uint8_t c0 = (idx < sizeof(cfg->wifi_password)) ? (uint8_t)cfg->wifi_password[idx] : 0U;
        uint8_t c1 = (idx + 1U < sizeof(cfg->wifi_password)) ? (uint8_t)cfg->wifi_password[idx + 1U] : 0U;
        *data = (uint16_t)((uint16_t)c0 << 8 | c1);
        return 0U;
    }
    case REG_TCP_IP_HIGH: {
        uint8_t c0 = (uint8_t)cfg->tcp_server_ip[0];
        uint8_t c1 = (uint8_t)cfg->tcp_server_ip[1];
        *data = (uint16_t)((uint16_t)c0 << 8 | c1);
        return 0U;
    }
    case REG_TCP_IP_LOW: {
        uint8_t c0 = (uint8_t)cfg->tcp_server_ip[2];
        uint8_t c1 = (uint8_t)cfg->tcp_server_ip[3];
        *data = (uint16_t)((uint16_t)c0 << 8 | c1);
        return 0U;
    }
    case REG_TCP_PORT:
        *data = cfg->tcp_server_port;
        return 0U;
    case REG_REPORT_PERIOD:
        *data = cfg->report_period_ms;
        return 0U;
    case REG_LIGHT_OFFSET:
        *data = (uint16_t)cfg->light_adc_offset;
        return 0U;
    case REG_TEMP_OFFSET:
        *data = (uint16_t)cfg->temp_offset;
        return 0U;
    case REG_ULTRASONIC_PERIOD:
        *data = cfg->ultrasonic_period_ms;
        return 0U;
    case REG_ULTRASONIC_THRESHOLD:
        *data = cfg->ultrasonic_threshold_mm;
        return 0U;
    case REG_ULTRASONIC_ENABLE:
        *data = (uint16_t)cfg->ultrasonic_enable;
        return 0U;
    default:
        return 1U;   /* 非法地址 */
    }
}

/**
 * @brief  写寄存器
 * @retval 0=成功, 非0=错误码 */
static uint8_t reg_write(uint16_t reg, uint16_t data)
{
    app_config_t *cfg = APP_EEPROM_GetConfigRW();

    /* 写非字符串寄存器时，重置字符串写状态（避免半写入残留） */
    if ((reg < REG_WIFI_SSID_BASE) || (reg > REG_WIFI_PWD_BASE + 31)) {
        s_ssid_writing = 0U;
        s_pwd_writing = 0U;
    }

    switch (reg) {
    case REG_LED_STATE:
        write_led_state((uint8_t)(data & 0x0FU));
        cfg->led_state = (uint8_t)(data & 0x0FU);
        return APP_EEPROM_Save();
    case REG_WIFI_SSID_BASE ... (REG_WIFI_SSID_BASE + 15): {
        uint16_t idx = (reg - REG_WIFI_SSID_BASE) * 2U;
        if (idx >= sizeof(cfg->wifi_ssid)) return PROTO_HEX_ERR_INVALID_REG;
        if (!s_ssid_writing) {
            memset(s_ssid_buf, 0, sizeof(s_ssid_buf));
            s_ssid_writing = 1U;
        }
        if (data == 0x0000U) {
            /* 字符串结束：保存到配置并写 EEPROM */
            memcpy(cfg->wifi_ssid, s_ssid_buf, sizeof(cfg->wifi_ssid));
            cfg->wifi_ssid[sizeof(cfg->wifi_ssid) - 1U] = '\0';
            s_ssid_writing = 0U;
            BSP_UART1_Printf("[HexCmd] SSID set: %s\r\n", cfg->wifi_ssid);
            return APP_EEPROM_Save();
        }
        s_ssid_buf[idx] = (char)((data >> 8) & 0xFFU);
        s_ssid_buf[idx + 1U] = (char)(data & 0xFFU);
        return 0U;   /* 累积中，暂不保存 */
    }
    case REG_WIFI_PWD_BASE ... (REG_WIFI_PWD_BASE + 31): {
        uint16_t idx = (reg - REG_WIFI_PWD_BASE) * 2U;
        if (idx >= sizeof(cfg->wifi_password)) return PROTO_HEX_ERR_INVALID_REG;
        if (!s_pwd_writing) {
            memset(s_pwd_buf, 0, sizeof(s_pwd_buf));
            s_pwd_writing = 1U;
        }
        if (data == 0x0000U) {
            memcpy(cfg->wifi_password, s_pwd_buf, sizeof(cfg->wifi_password));
            cfg->wifi_password[sizeof(cfg->wifi_password) - 1U] = '\0';
            s_pwd_writing = 0U;
            BSP_UART1_Printf("[HexCmd] Password set\r\n");
            return APP_EEPROM_Save();
        }
        s_pwd_buf[idx] = (char)((data >> 8) & 0xFFU);
        s_pwd_buf[idx + 1U] = (char)(data & 0xFFU);
        return 0U;
    }
    case REG_TCP_IP_HIGH:
        s_tcp_ip_high = data;
        return 0U;   /* 暂存，等低16位到达后组合保存 */
    case REG_TCP_IP_LOW: {
        cfg->tcp_server_ip[0] = (char)((s_tcp_ip_high >> 8) & 0xFFU);
        cfg->tcp_server_ip[1] = (char)(s_tcp_ip_high & 0xFFU);
        cfg->tcp_server_ip[2] = (char)((data >> 8) & 0xFFU);
        cfg->tcp_server_ip[3] = (char)(data & 0xFFU);
        cfg->tcp_server_ip[4] = '\0';
        BSP_UART1_Printf("[HexCmd] TCP IP set: %d.%d.%d.%d\r\n",
                         (uint8_t)cfg->tcp_server_ip[0], (uint8_t)cfg->tcp_server_ip[1],
                         (uint8_t)cfg->tcp_server_ip[2], (uint8_t)cfg->tcp_server_ip[3]);
        return APP_EEPROM_Save();
    }
    case REG_TCP_PORT:
        if (data == 0U) return PROTO_HEX_ERR_INVALID_VALUE;
        cfg->tcp_server_port = data;
        BSP_UART1_Printf("[HexCmd] TCP port set: %u\r\n", (unsigned)data);
        return APP_EEPROM_Save();
    case REG_REPORT_PERIOD:
        if (data < 500U) return PROTO_HEX_ERR_INVALID_VALUE;
        cfg->report_period_ms = data;
        BSP_UART1_Printf("[HexCmd] Report period set: %ums\r\n", (unsigned)data);
        return APP_EEPROM_Save();
    case REG_LIGHT_OFFSET:
        cfg->light_adc_offset = (int16_t)data;
        return APP_EEPROM_Save();
    case REG_TEMP_OFFSET:
        cfg->temp_offset = (int16_t)data;
        return APP_EEPROM_Save();
    case REG_ULTRASONIC_PERIOD:
        if (data < 100U) return PROTO_HEX_ERR_INVALID_VALUE;  /* 最小100ms */
        cfg->ultrasonic_period_ms = data;
        BSP_UART1_Printf("[HexCmd] Ultrasonic period set: %ums\r\n", (unsigned)data);
        return APP_EEPROM_Save();
    case REG_ULTRASONIC_THRESHOLD:
        if (data < 20U) return PROTO_HEX_ERR_INVALID_VALUE;  /* 最小有效距离20mm */
        cfg->ultrasonic_threshold_mm = data;
        BSP_UART1_Printf("[HexCmd] Ultrasonic threshold set: %umm\r\n", (unsigned)data);
        return APP_EEPROM_Save();
    case REG_ULTRASONIC_ENABLE:
        cfg->ultrasonic_enable = (data != 0U) ? 1U : 0U;
        BSP_UART1_Printf("[HexCmd] Ultrasonic %s\r\n", cfg->ultrasonic_enable ? "enabled" : "disabled");
        return APP_EEPROM_Save();
    case REG_RESET_DEFAULT:
        if (data == 0xA5A5U) {
            BSP_UART1_Printf("[HexCmd] Reset to default config\r\n");
            return APP_EEPROM_ResetDefault();
        }
        return PROTO_HEX_ERR_INVALID_VALUE;
    default:
        return PROTO_HEX_ERR_INVALID_REG;
    }
}

/* ========== 公共 API ========== */

uint8_t APP_HexCmd_Process(const uint8_t *in_frame, uint8_t *out_frame)
{
    uint8_t cmd = PROTO_HEX_GET_CMD(in_frame);
    uint16_t reg = PROTO_HEX_GET_REG(in_frame);
    uint16_t data = PROTO_HEX_GET_DATA(in_frame);
    uint8_t seq = PROTO_HEX_GET_SEQ(in_frame);
    uint8_t dev_addr = PROTO_HEX_GET_DEVADDR(in_frame);

    /* 广播地址不应答 */
    if (dev_addr == PROTO_HEX_BROADCAST_ADDR) {
        return 0U;
    }

    /* 设备地址不匹配（非广播），不应答 */
    if (dev_addr != PROTO_HEX_DEVICE_ADDR) {
        return 0U;
    }

    switch (cmd) {
    case PROTO_HEX_CMD_READ_REG: {
        uint16_t val;
        if (reg_read(reg, &val) == 0U) {
            ProtoHex_BuildFrame(out_frame, PROTO_HEX_DEVICE_ADDR,
                                PROTO_HEX_CMD_READ_REG, reg, val, seq, 1U);
        } else {
            ProtoHex_BuildFrame(out_frame, PROTO_HEX_DEVICE_ADDR,
                                PROTO_HEX_CMD_ERROR, reg, PROTO_HEX_ERR_INVALID_REG, seq, 1U);
        }
        return 1U;
    }
    case PROTO_HEX_CMD_WRITE_REG: {
        uint8_t err = reg_write(reg, data);
        if (err == 0U) {
            ProtoHex_BuildFrame(out_frame, PROTO_HEX_DEVICE_ADDR,
                                PROTO_HEX_CMD_ACK, reg, PROTO_HEX_CMD_WRITE_REG, seq, 1U);
        } else {
            ProtoHex_BuildFrame(out_frame, PROTO_HEX_DEVICE_ADDR,
                                PROTO_HEX_CMD_ERROR, reg, err, seq, 1U);
        }
        return 1U;
    }
    case PROTO_HEX_CMD_LED_CTRL: {
        write_led_state((uint8_t)(data & 0x0FU));
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
        app_config_t *cfg = APP_EEPROM_GetConfigRW();
        cfg->led_state = (uint8_t)(data & 0x0FU);
        (void)APP_EEPROM_Save();
#endif
        uint16_t cur = (uint16_t)read_led_state();
        ProtoHex_BuildFrame(out_frame, PROTO_HEX_DEVICE_ADDR,
                            PROTO_HEX_CMD_LED_CTRL, reg, cur, seq, 1U);
        return 1U;
    }
    case PROTO_HEX_CMD_HEARTBEAT: {
        const app_config_t *cfg = APP_EEPROM_GetConfig();
        uint16_t rt = (uint16_t)(cfg->runtime_seconds & 0xFFFFU);
        ProtoHex_BuildFrame(out_frame, PROTO_HEX_DEVICE_ADDR,
                            PROTO_HEX_CMD_HEARTBEAT, reg, rt, seq, 1U);
        return 1U;
    }
    case PROTO_HEX_CMD_SENSOR_REPORT:
        /* 传感器上报是设备主动发出的，收到上报帧不应答 */
        return 0U;
    default:
        ProtoHex_BuildFrame(out_frame, PROTO_HEX_DEVICE_ADDR,
                            PROTO_HEX_CMD_ERROR, reg, PROTO_HEX_ERR_INVALID_CMD, seq, 1U);
        return 1U;
    }
}

uint8_t APP_HexCmd_BuildReport(uint8_t *out_frame, uint16_t reg, uint16_t data)
{
    static uint8_t s_report_seq = 0U;
    s_report_seq++;
    if (s_report_seq > 127U) s_report_seq = 0U;
    return ProtoHex_BuildFrame(out_frame, PROTO_HEX_DEVICE_ADDR,
                               PROTO_HEX_CMD_SENSOR_REPORT, reg, data,
                               s_report_seq, 0U);
}

#endif /* PROTOCOL_ENABLE */
