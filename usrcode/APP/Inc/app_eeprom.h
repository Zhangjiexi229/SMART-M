/**
 ******************************************************************************
 * @file    app_eeprom.h
 * @brief   EEPROM 配置存取模块头文件—AT24C02 配置管理
 *
 *  存储布局（EEPROM 256字节）：
 *    0x0000~0x00FF  配置块（app_config_t，含魔数/版本/CRC16）
 *    0x0100~0x7FFF  预留 / 数据日志区
 *
 *  配置块内容：
 *    - 头部：魔数(0xAA55AA55) + 版本 + CRC16
 *    - 网络配置：WiFi SSID/密码、TCP服务器IP/端口、上报周期、设备ID
 *    - 设备状态：LED状态、控灯使能开关
 *    - 校准参数：光照ADC偏移/增益、温度补偿、光照阈值
 *    - 运行统计：运行时间、重启次数、WiFi失败次数
 *
 *  可靠性设计：
 *    - CRC16 校验整块配置，上电校验失败自动加载默认值
 *    - 写操作仅在配置变更时触发，避免频繁写磨损
 *    - 运行统计建议每小时聚合写一次（由调用方控制频率）
 ******************************************************************************
 */
#ifndef APP_EEPROM_H
#define APP_EEPROM_H

#include <stdint.h>

/* ========== 存储地址定义 ========== */
#define EEPROM_CONFIG_ADDR      0x0000U   /* 配置块起始地址 */
#define EEPROM_CONFIG_MAGIC     0xAA55AA55U  /* 配置块魔数 */
#define EEPROM_CONFIG_VERSION   0x0001U   /* 配置格式版本 */

/* ========== 配置结构体（紧凑存储，1字节对齐）========== */
#pragma pack(push, 1)
typedef struct {
    /* ---- 头部（8字节）---- */
    uint32_t magic;              /* 固定 0xAA55AA55，判断是否已初始化 */
    uint16_t version;            /* 配置格式版本，升级固件时用于兼容性判断 */
    uint16_t crc;                /* CRC16-CCITT，覆盖magic 之后到crc 之前的所有字段 */

    /* ---- 网络配置（124字节）---- */
    char     wifi_ssid[32];      /* WiFi SSID（含结束符） */
    char     wifi_password[64];  /* WiFi 密码（含结束符） */
    char     tcp_server_ip[16];  /* TCP服务器IP，如 "192.168.168.3" */
    uint16_t tcp_server_port;    /* TCP服务器端口 */
    uint16_t report_period_ms;   /* 传感器上报周期（毫秒） */
    uint8_t  device_id[8];       /* 设备唯一ID（多设备区分） */

    /* ---- 设备状态（4字节）---- */
    uint8_t  led_state;          /* LED状态位域：bit0=LED1, bit1=LED2, bit2=LED3, bit3=LED4 (1=亮) */
    uint8_t  wifi_led_enable;    /* WiFi远程控灯使能 */
    uint8_t  uart1_led_enable;   /* 串口1控灯使能 */
    uint8_t  auto_mode;          /* 自动模式标志（预留，如暗光自动开灯） */

    /* ---- 校准参数（10字节）---- */
    int16_t  light_adc_offset;   /* 光照ADC偏移校准 */
    int16_t  light_adc_gain;     /* 光照ADC增益（Q8格式，256=1.0倍） */
    int16_t  temp_offset;        /* 温度补偿偏移（单位0.1度，如 -5 = -0.5度） */
    uint16_t light_threshold_dark;   /* 暗光阈值（ADC值） */
    uint16_t light_threshold_bright; /* 亮光阈值（ADC值） */

    /* ---- 运行统计（8字节）---- */
    uint32_t runtime_seconds;    /* 累计运行时间（秒）*/
    uint16_t reboot_count;       /* 累计重启次数 */
    uint16_t wifi_fail_count;    /* WiFi连接失败次数 */

    /* ---- 超声波配置（6字节，从原reserved区分配） ---- */
    uint16_t ultrasonic_period_ms;     /* 超声波测量周期(ms)，默认500，最小100 */
    uint16_t ultrasonic_threshold_mm;  /* 超声波距离报警阈值(mm)，默认300 */
    uint8_t  ultrasonic_enable;        /* 超声波模块使能，1=开启, 0=关闭 */
    uint8_t  ultrasonic_reserved;      /* 对齐填充 */

    /* ---- 预留（26字节）---- */
    uint8_t  reserved[26];
} app_config_t;
#pragma pack(pop)

/* ========== API ========== */

/**
 * @brief  初始化 EEPROM 配置模块
 * @note   初始化AT24C02硬件 →读取配置块→CRC校验 →失败则加载默认值并写回
 * @retval 0=成功(从EEPROM加载), 1=使用默认值(EEPROM未初始化或CRC错误)
 */
uint8_t APP_EEPROM_Init(void);

/**
 * @brief  获取当前配置（RAM中的只读指针）
 * @retval 配置结构体指针
 * @note   修改配置后需调用 APP_EEPROM_Save() 才会写入EEPROM
 */
const app_config_t *APP_EEPROM_GetConfig(void);

/**
 * @brief  获取当前配置的可修改指针
 * @retval 配置结构体指针（可修改）
 * @note   修改字段后必须调用APP_EEPROM_Save() 保存到EEPROM
 */
app_config_t *APP_EEPROM_GetConfigRW(void);

/**
 * @brief  保存当前配置到 EEPROM（自动计算CRC）
 * @retval 0=成功, 非0=写入失败
 */
uint8_t APP_EEPROM_Save(void);

/**
 * @brief  恢复默认配置并写入 EEPROM
 * @retval 0=成功, 非0=写入失败
 */
uint8_t APP_EEPROM_ResetDefault(void);

/* ---- LED 状态便捷接口 ---- */

/**
 * @brief  获取保存的LED状态
 * @param  led  LED编号 (0~3)
 * @retval 1=亮, 0=灭
 */
uint8_t APP_EEPROM_GetLEDState(uint8_t led);

/**
 * @brief  设置LED状态并保存到 EEPROM
 * @param  led    LED编号 (0~3)
 * @param  state  1=亮, 0=灭
 * @retval 0=成功, 非0=写入失败
 */
uint8_t APP_EEPROM_SetLEDState(uint8_t led, uint8_t state);

/**
 * @brief  批量设置所有LED状态并保存（仅写一次 EEPROM）
 * @param  state_mask  状态位域：bit0=LED1, bit1=LED2, bit2=LED3, bit3=LED4
 * @retval 0=成功, 非0=写入失败
 */
uint8_t APP_EEPROM_SetLEDStateAll(uint8_t state_mask);

/* ---- 运行统计便捷接口 ---- */

/**
 * @brief  增加累计运行时间并保存
 * @param  seconds  增加的秒数
 * @retval 0=成功, 非0=写入失败
 */
uint8_t APP_EEPROM_AddRuntime(uint32_t seconds);

/**
 * @brief  增加重启计数并保存（上电时调用一次）
 * @retval 0=成功, 非0=写入失败
 */
uint8_t APP_EEPROM_IncrementReboot(void);

#endif /* APP_EEPROM_H */
