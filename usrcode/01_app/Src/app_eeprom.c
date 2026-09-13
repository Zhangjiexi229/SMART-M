/**
 ******************************************************************************
 * @file    app_eeprom.c
 * @brief   EEPROM 配置存取模块实现 — AT24C02 配置管理
 *
 *  流程：
 *    APP_EEPROM_Init()
 *      →BSP_AT24C02_Init()
 *      →BSP_AT24C02_Read(配置块)
 *      →校验 magic + version + CRC16
 *      →校验通过：加载到RAM
 *      →校验失败：加载默认值 → 计算CRC → 写回 EEPROM
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_EEPROM_ENABLE

#include "app_eeprom.h"
#include "bsp_at24c02.h"
#include "bsp_uart.h"
#include <string.h>

/* ========== 默认配置（与 app_wifi.h 中的宏保持一致） ========== */
static const app_config_t s_default_config = {
    .magic              = EEPROM_CONFIG_MAGIC,
    .version            = EEPROM_CONFIG_VERSION,
    .crc                = 0U,

    /* 网络配置默认值 */
    .wifi_ssid          = "LAPTOP-2EPLM3P0 2316",
    .wifi_password      = "your_wifi_password",
    .tcp_server_ip      = "192.168.168.3",
    .tcp_server_port    = 8080U,
    .report_period_ms   = 3000U,
    .device_id          = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},

    /* 设备状态默认值 */
    .led_state          = 0x00U,    /* 所有LED默认灭 */
    .wifi_led_enable    = 1U,       /* WiFi控灯默认使能 */
    .uart1_led_enable   = 1U,       /* 串口1控灯默认使能 */
    .auto_mode          = 0U,       /* 自动模式默认关闭 */

    /* 校准参数默认值 */
    .light_adc_offset   = 0,
    .light_adc_gain     = 256,      /* Q8: 1.0倍 */
    .temp_offset        = 0,
    .light_threshold_dark   = 500U,
    .light_threshold_bright = 2000U,

    /* 运行统计默认值 */
    .runtime_seconds    = 0U,
    .reboot_count       = 0U,
    .wifi_fail_count    = 0U,

    /* 超声波配置默认值 */
    .ultrasonic_period_ms    = 500U,   /* 测量周期500ms */
    .ultrasonic_threshold_mm = 300U,   /* 报警阈值300mm */
    .ultrasonic_enable       = 1U,     /* 默认使能 */
    .ultrasonic_reserved     = 0U,

    .reserved           = {0},
};

/* ========== RAM 中的当前配置 ========== */
static app_config_t s_config;

/* ========== CRC16-CCITT (poly=0x1021, init=0xFFFF) ========== */
static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t j;

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0U; j < 8U; j++) {
            if (crc & 0x8000U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief  计算配置块的CRC（覆盖magic 之后到crc 之前的字段）
 */
static uint16_t config_calc_crc(const app_config_t *cfg)
{
    /* CRC 计算范围：从 version 字段开始，到crc 字段之前结束 */
    const uint8_t *p = (const uint8_t *)cfg;
    uint32_t crc_offset = offsetof(app_config_t, version);
    uint32_t crc_field_offset = offsetof(app_config_t, crc);
    return crc16_ccitt(&p[crc_offset], crc_field_offset - crc_offset);
}

/**
 * @brief  加载默认配置到RAM
 */
static void load_default_config(void)
{
    memcpy(&s_config, &s_default_config, sizeof(app_config_t));
    s_config.crc = config_calc_crc(&s_config);
}

/**
 * @brief  将当前配置写入EEPROM并回读验证（用于初始化阶段诊断写失败）
 * @retval 0=写且验证通过, 1=写失败 2=回读验证失败
 */
static uint8_t write_config_and_verify(void)
{
    uint8_t st;
    uint8_t verify[8];
    const uint8_t *wbuf = (const uint8_t *)&s_config;

    st = BSP_AT24C02_Write(EEPROM_CONFIG_ADDR, (const uint8_t *)&s_config, sizeof(app_config_t));
    if (st != 0U) {
        BSP_UART1_Printf("[EEPROM] Write failed (ret=%u), check I2C wiring\r\n", (unsigned)st);
        return 1U;
    }

    /* 回读前8字节（magic+version+crc），逐字节比对诊断*/
    st = BSP_AT24C02_Read(EEPROM_CONFIG_ADDR, verify, sizeof(verify));
    if (st != 0U) {
        BSP_UART1_Printf("[EEPROM] Verify read failed (ret=%u)\r\n", (unsigned)st);
        return 2U;
    }

    if (memcmp(wbuf, verify, sizeof(verify)) != 0U) {
        BSP_UART1_Printf("[EEPROM] Verify FAILED:\r\n");
        BSP_UART1_Printf("  wrote: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                         wbuf[0], wbuf[1], wbuf[2], wbuf[3], wbuf[4], wbuf[5], wbuf[6], wbuf[7]);
        BSP_UART1_Printf("  read : %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                         verify[0], verify[1], verify[2], verify[3], verify[4], verify[5], verify[6], verify[7]);
        BSP_UART1_Printf("  -- WP=GND but still fail: check pull-up R33/R34 (rec <=4.7K), or SDA short to GND\r\n");
        return 2U;
    }
    return 0U;
}

/* ========== 公共 API ========== */

uint8_t APP_EEPROM_Init(void)
{
    uint8_t st;
    uint16_t calc_crc;

    /* 初始化硬件 */
    BSP_AT24C02_Init();

    /* 总线空闲电平诊断：释放SDA/SCL后读取，正常应都为高(0x03) */
    {
        uint8_t bus = BSP_AT24C02_CheckBusLevel();
        BSP_UART1_Printf("[EEPROM] Bus idle level: SDA=%s SCL=%s (0x%02X)\r\n",
                         (bus & 0x01U) ? "HIGH" : "LOW",
                         (bus & 0x02U) ? "HIGH" : "LOW",
                         (unsigned)bus);
        if ((bus & 0x01U) == 0U) {
            BSP_UART1_Printf("[EEPROM] WARNING: SDA is LOW at idle! SDA may be shorted to GND or pulled down by another device\r\n");
        }
    }

    /* 检测芯片是否就绪 */
    if (BSP_AT24C02_CheckReady() != 0U) {
        BSP_UART1_Printf("[EEPROM] AT24C02 not responding, use default config\r\n");
        load_default_config();
        return 1U;
    }

    /* 读取配置块 */
    st = BSP_AT24C02_Read(EEPROM_CONFIG_ADDR, (uint8_t *)&s_config, sizeof(app_config_t));
    if (st != 0U) {
        BSP_UART1_Printf("[EEPROM] Read failed (ret=%u), use default config\r\n", (unsigned)st);
        load_default_config();
        (void)write_config_and_verify();
        return 1U;
    }

    /* 校验魔数和版本 */
    if ((s_config.magic != EEPROM_CONFIG_MAGIC) || (s_config.version != EEPROM_CONFIG_VERSION)) {
        BSP_UART1_Printf("[EEPROM] Magic/version mismatch (magic=0x%08X ver=%u), init default\r\n",
                         (unsigned)s_config.magic, (unsigned)s_config.version);
        load_default_config();
        (void)write_config_and_verify();
        return 1U;
    }

    /* 校验CRC */
    calc_crc = config_calc_crc(&s_config);
    if (calc_crc != s_config.crc) {
        BSP_UART1_Printf("[EEPROM] CRC error (calc=0x%04X stored=0x%04X), restore default\r\n",
                         (unsigned)calc_crc, (unsigned)s_config.crc);
        load_default_config();
        (void)write_config_and_verify();
        return 1U;
    }

    /* 旧芯片兼容：原reserved区全0，超声波参数未初始化，填充默认值 */
    if (s_config.ultrasonic_period_ms == 0U) {
        s_config.ultrasonic_period_ms    = 500U;
        s_config.ultrasonic_threshold_mm = 300U;
        s_config.ultrasonic_enable       = 1U;
        (void)APP_EEPROM_Save();
        BSP_UART1_Printf("[EEPROM] Ultrasonic params initialized to default\r\n");
    }

    BSP_UART1_Printf("[EEPROM] Config loaded OK (reboot=%u, runtime=%lus)\r\n",
                     (unsigned)s_config.reboot_count, (unsigned long)s_config.runtime_seconds);
    return 0U;
}

const app_config_t *APP_EEPROM_GetConfig(void)
{
    return &s_config;
}

app_config_t *APP_EEPROM_GetConfigRW(void)
{
    return &s_config;
}

uint8_t APP_EEPROM_Save(void)
{
    uint8_t st;
    s_config.crc = config_calc_crc(&s_config);
    st = BSP_AT24C02_Write(EEPROM_CONFIG_ADDR, (const uint8_t *)&s_config, sizeof(app_config_t));
    if (st != 0U) {
        BSP_UART1_Printf("[EEPROM] Save failed!\r\n");
    }
    return st;
}

uint8_t APP_EEPROM_ResetDefault(void)
{
    load_default_config();
    return APP_EEPROM_Save();
}

/* ---- LED 状态接口 ---- */

uint8_t APP_EEPROM_GetLEDState(uint8_t led)
{
    if (led >= 4U) return 0U;
    return (s_config.led_state & (1U << led)) ? 1U : 0U;
}

uint8_t APP_EEPROM_SetLEDState(uint8_t led, uint8_t state)
{
    if (led >= 4U) return 1U;
    if (state) {
        s_config.led_state |= (uint8_t)(1U << led);
    } else {
        s_config.led_state &= (uint8_t)~(1U << led);
    }
    return APP_EEPROM_Save();
}

uint8_t APP_EEPROM_SetLEDStateAll(uint8_t state_mask)
{
    s_config.led_state = state_mask & 0x0FU;
    return APP_EEPROM_Save();
}

/* ---- 运行统计接口 ---- */

uint8_t APP_EEPROM_AddRuntime(uint32_t seconds)
{
    s_config.runtime_seconds += seconds;
    return APP_EEPROM_Save();
}

uint8_t APP_EEPROM_IncrementReboot(void)
{
    s_config.reboot_count++;
    return APP_EEPROM_Save();
}

#endif /* APP_EEPROM_ENABLE */
