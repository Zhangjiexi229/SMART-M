/**
 ******************************************************************************
 * @file    module_cfg.h
 * @brief   条件编译模块开关配置 — 统一管控各层模块的启用/禁用
 *  使用说明：
 *    - 每个源文件顶部包含本头文件，用 #if 包裹全部实现
 *    - 禁用模块不生成任何代码，节省 Flash 和 RAM
 *    - 便于按实际硬件配置裁剪固件
 ******************************************************************************
 */
#ifndef MODULE_CFG_H
#define MODULE_CFG_H

/* ========== BSP 板级支持包模块开关 ========== */
#define BSP_DELAY_ENABLE        1   /* 板级微秒/毫秒延时（共用TIM2自由计数器，bsp_delay） */
#define BSP_LED_ENABLE          1   /* LED灯驱动（总开关；关闭后所有模块均无法控制LED） */
#define BSP_UART1_ENABLE        1   /* UART1驱动（DMA环形接收 + IDLE判帧，调试打印用；WiFi任务依赖） */
#define BSP_UART3_ENABLE        1   /* UART3驱动（DMA环形接收 + IDLE判帧，ESP8266通信口） */
#define BSP_ESP8266_ENABLE      1   /* ESP8266 WiFi模块AT指令驱动（依赖BSP_UART3） */
#define BSP_KEY_ENABLE          0   /* 按键驱动（GPIO读取+消抖状态机）——F407VET6无PG端口，独立键停用；板载按键 WK_UP=PA0/KEY0=PE4/KEY1=PE3 */
#define BSP_KEY_MATRIX_ENABLE   1   /* 4x4矩阵键盘驱动（行PE2/PE6/PE7/PE10，列PE8/PE9/PE11/PE12，行扫描+列读取；避开板载KEY0=PE4/KEY1=PE3与Echo=PE5） */
#define BSP_BEEP_ENABLE         1   /* 有源蜂鸣器驱动（PA5，NPN三极管驱动，高电平响） */
#define BSP_LIGHT_SENSOR_ENABLE 0   /* 光敏传感器驱动（ADC3_IN5/PF7，查询式单次转换） */
#define BSP_DHT11_ENABLE        0   /* DHT11温湿度驱动——F407VET6无PG9引脚，且SHT30已提供温湿度，停用 */
#define BSP_FLASH_CONFIG_ENABLE 1   /* 内部Flash配置存储（Sector11@0x080E0000，存告警阈值，上电加载） */
#define BSP_I2C_SOFT_ENABLE     1   /* 共享软件I2C总线驱动（PB8=SCL/PB9=SDA，与AT24C02同总线，为SHT30/QMI8658/INA226提供主机时序） */
#define BSP_SHT30_ENABLE        1   /* SHT30温湿度传感器驱动（软件I2C，7位地址0x44，ADDR接GND；依赖BSP_I2C_SOFT_ENABLE+BSP_DELAY_ENABLE） */
#define BSP_QMI8658_ENABLE      1   /* QMI8658六轴IMU驱动（软件I2C，7位地址0x6B，本模块AD0板上固定接地；依赖BSP_I2C_SOFT_ENABLE） */
#define BSP_INA226_ENABLE       1   /* INA226电源监测驱动（软件I2C，7位地址0x40，A0/A1接GND；采样电阻0.1Ω；依赖BSP_I2C_SOFT_ENABLE） */
#define BSP_RELAY_ENABLE        1   /* 继电器驱动（PA4推挽输出，高电平吸合；COM/NO串接电机电源回路） */
#define BSP_OLED_ENABLE         1   /* OLED驱动（SSD1306，PD6=SCL/PD7=SDA 软件I2C） */
#define BSP_AT24C02_ENABLE      0   /* I2C EEPROM驱动——新核心板无AT24C02芯片，停用 */
#define BSP_W25Q128_ENABLE      0   /* SPI Flash W25Q128——新板W25Q128为板载(SPI1)，工程不使用，停用 */
#define BSP_HC_SR04_ENABLE      0   /* HC-SR04超声波测距驱动（PC11=Trig, PE5=Echo，轮询测量） */
/* ========== Protocol 协议层模块开关 ========== */
#define PROTOCOL_ENABLE         0   /* 字符指令解析协议——新板适配精简，串口指令协议停用（毕设主线为MQTT云） */

/* ========== APP 应用层模块开关 ========== */
#define APP_TASKS_ENABLE        1   /* FreeRTOS任务管理 */
#define APP_UART1_CMD_ENABLE    0   /* 串口1指令接LED——随PROTOCOL_ENABLE=0停用 */
#define APP_UART3_CMD_ENABLE    0   /* 串口3指令控LED（与ESP8266共用UART3硬件，互斥；启用前需将BSP_ESP8266_ENABLE设为0；WiFi远程控灯请用APP_WIFI_LED_ENABLE） */

/* 互斥保护：UART3 同时只能有一个消费者（ESP8266驱动 或 指令控LED任务），
 * 两者同时启用会争抢同一个软件环形缓冲，导致AT响应丢包、WiFi连接失败 */
#if BSP_ESP8266_ENABLE && APP_UART3_CMD_ENABLE
#error "冲突：BSP_ESP8266_ENABLE 与 APP_UART3_CMD_ENABLE 不能同时为1，UART3只能用于ESP8266通信或串口指令控LED二者之一"
#endif
/* MQTT任务与WiFi TCP上报任务共用ESP8266，不能同时运行 */
#if APP_MQTT_ENABLE && APP_WIFI_ENABLE
#error "冲突：APP_MQTT_ENABLE 与 APP_WIFI_ENABLE 不能同时为1（共用ESP8266，二选一）"
#endif
/* 软件I2C器件（SHT30/QMI8658/INA226）依赖共享软件I2C总线与微秒延时 */
#if (BSP_SHT30_ENABLE || BSP_QMI8658_ENABLE || BSP_INA226_ENABLE) && (!BSP_I2C_SOFT_ENABLE || !BSP_DELAY_ENABLE)
#error "依赖缺失：BSP_SHT30_ENABLE/BSP_QMI8658_ENABLE/BSP_INA226_ENABLE 依赖 BSP_I2C_SOFT_ENABLE=1 与 BSP_DELAY_ENABLE=1"
#endif
/* 毕设诊断链依赖：告警任务依赖诊断中枢+阈值配置 */
#if APP_ALARM_ENABLE && (!APP_DIAG_ENABLE || !APP_CONFIG_ENABLE)
#error "依赖缺失：APP_ALARM_ENABLE 依赖 APP_DIAG_ENABLE=1 与 APP_CONFIG_ENABLE=1"
#endif
/* 矩阵键盘应用依赖矩阵键盘BSP */
#if APP_KEY_MATRIX_ENABLE && !BSP_KEY_MATRIX_ENABLE
#error "依赖缺失：APP_KEY_MATRIX_ENABLE 依赖 BSP_KEY_MATRIX_ENABLE=1"
#endif
/* 故障诊断依赖振动特征 */
#if APP_FAULT_ENABLE && !APP_VIBRATION_ENABLE
#error "依赖缺失：APP_FAULT_ENABLE 依赖 APP_VIBRATION_ENABLE=1"
#endif
/* 断网补传依赖诊断中枢 */
#if APP_OFFLINE_ENABLE && !APP_DIAG_ENABLE
#error "依赖缺失：APP_OFFLINE_ENABLE 依赖 APP_DIAG_ENABLE=1"
#endif
/* 阈值配置依赖内部Flash存储 */
#if APP_CONFIG_ENABLE && !BSP_FLASH_CONFIG_ENABLE
#error "依赖缺失：APP_CONFIG_ENABLE 依赖 BSP_FLASH_CONFIG_ENABLE=1"
#endif
#define LED_BLINK_TASK_ENABLE   0   /* LED闪烁任务（LED1/LED2固定周期闪烁，受APP_TASKS_ENABLE约束）；注意：会与串口指令控制LED冲突，串口控制场景请保持0 */
#define APP_KEY_ENABLE          0   /* 按键扫描任务（依赖BSP_KEY_ENABLE）——F407VET6无PG端口，独立键停用，按键功能由4x4矩阵键盘承担 */
#define APP_LIGHT_SENSOR_ENABLE 0   /* 光敏传感器采集任务（周期采集+串口打印+LED分级控制+OLED显示，受APP_TASKS_ENABLE约束，依赖BSP_LIGHT_SENSOR_ENABLE=1） */
#define APP_DHT11_ENABLE        0   /* DHT11温湿度采集任务（依赖BSP_DHT11_ENABLE）——停用 */
#define APP_SENSOR_ENABLE       1   /* 统一传感器快照层（所有传感器数据集中存放，MQTT/WiFi任务一次临界区读取；app_dht11已引用） */
#define APP_SHT30_ENABLE        1   /* SHT30温湿度采集任务（周期采集+串口打印+共享快照，受APP_TASKS_ENABLE约束，依赖BSP_SHT30_ENABLE=1） */
#define APP_QMI8658_ENABLE      1   /* QMI8658六轴IMU采集任务（周期采集+姿态解算+串口打印+共享快照，受APP_TASKS_ENABLE约束，依赖BSP_QMI8658_ENABLE=1） */
#define APP_INA226_ENABLE       1   /* INA226电源监测任务（周期采集+过流自动断电保护+串口打印+共享快照，受APP_TASKS_ENABLE约束，依赖BSP_INA226_ENABLE=1） */
#define APP_RELAY_ENABLE        1   /* 继电器应用（统一控制入口+状态快照，供MQTT下行命令/过流保护调用，依赖BSP_RELAY_ENABLE=1） */
#define APP_OLED_ENABLE         1   /* OLED显示应用（实时/阈值/状态三页面 + 告警闪烁） */
#define APP_DIAG_ENABLE         1   /* 诊断数据中枢（float传感器快照 + 全局告警标志，供告警/诊断/断网补传/OLED使用） */
#define APP_CONFIG_ENABLE       1   /* 阈值配置（默认温度60/振动3/电流5，按键可调并存入内部Flash，依赖BSP_FLASH_CONFIG_ENABLE=1） */
#define APP_ALARM_ENABLE        1   /* 告警联动任务（三路阈值+迟滞恢复+继电器断电保护+蜂鸣器，受APP_TASKS_ENABLE约束，依赖APP_DIAG_ENABLE+APP_CONFIG_ENABLE=1） */
#define APP_FILTER_ENABLE       1   /* 滑动平均滤波器（传感器去抖，10点窗口） */
#define APP_VIBRATION_ENABLE    1   /* 振动特征提取（1秒窗口：RMS/峰值/峭度/峰值因子，供故障诊断） */
#define APP_FAULT_ENABLE        1   /* 故障诊断规则引擎（正常/注意/异常 + 过热/过载/振动/不平衡/轴承位图，依赖APP_VIBRATION_ENABLE=1） */
#define APP_OFFLINE_ENABLE      1   /* 断网补传缓冲（RAM环形300条，重连后批量补发，依赖APP_DIAG_ENABLE=1） */
#define APP_KEY_MATRIX_ENABLE   1   /* 矩阵键盘应用任务（消抖+页面切换/继电器/蜂鸣器/阈值设置，受APP_TASKS_ENABLE约束，依赖BSP_KEY_MATRIX_ENABLE=1） */
#define APP_WIFI_ENABLE         0   /* WiFi上报任务（ESP8266连路由器+TCP上报温湿度，受APP_TASKS_ENABLE约束，依赖BSP_ESP8266_ENABLE=1） */
#define APP_MQTT_ENABLE         1   /* MQTT上云任务（ESP8266+Paho MQTT连接云平台，受APP_TASKS_ENABLE约束，依赖BSP_ESP8266_ENABLE=1，与APP_WIFI_ENABLE互斥） */
#define APP_MODBUS_ENABLE       0   /* Modbus主从一体任务——新板PB1被LCD背光上拉(R13 15K)占用，毕设主线为MQTT云，停用 */
#define APP_EEPROM_ENABLE       0   /* EEPROM配置存取模块（依赖BSP_AT24C02_ENABLE）——新板无AT24C02，停用 */
#define APP_HC_SR04_ENABLE      0   /* 超声波采集任务（周期测量+串口打印+协议上报+可选LED控制，受APP_TASKS_ENABLE约束，依赖BSP_HC_SR04_ENABLE=1） */

/* ========== 调试开关 ========== */
#define APP_HEX_CMD_DEBUG       0   /* hex帧调试打印：收到/发送hex帧时以十六进制文本打印到串口1（调试用，正式版关闭避免干扰二进制通信） */

/* ========== 各模块 LED 控制独立开关 ==========
 *  仅控制对应模块是否执行 LED 动作，不影响 LED 驱动本身（BSP_LED_ENABLE）。
 *  关闭后该模块仍正常运行（采集/打印/扫描），只是不操作 LED。
 *  需同时满足 BSP_LED_ENABLE=1 才生效。
 */
#define APP_KEY_LED_ENABLE      0   /* 按键控LED（KEY0→LED1亮, KEY1→LED1灭, KEY2→LED2亮, KEY3→LED2灭） */
#define APP_UART1_LED_ENABLE    0   /* 串口1指令控LED——随协议停用 */
#define APP_UART3_LED_ENABLE    1   /* 串口3指令控LED（LED1ON/LED1OFF/LED2ON/LED2OFF/ALLON/ALLOFF，需先禁用ESP8266） */
#define APP_WIFI_LED_ENABLE     1   /* WiFi远程控LED（TCP服务器端发送LED1ON等指令，经ESP8266+IPD解析后执行） */
#define APP_LIGHT_LED_ENABLE    0   /* 光敏传感器控LED（暗光→双灯亮, 中等光→LED2亮, 强光→双灯灭） */
#define APP_DHT11_LED_ENABLE    0   /* 温湿度传感器控LED（高温→LED1+LED2亮, 高湿→LED3+LED4亮） */
#define APP_HC_SR04_LED_ENABLE  0   /* 超声波控LED（距离小于阈值→LED1亮，障碍物报警） */



#endif /* MODULE_CFG_H */
