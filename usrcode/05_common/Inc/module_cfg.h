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
#define BSP_BT24_ENABLE         1   /* DX-BT24 BLE串口透传模块驱动（USART2/PA2-PA3，DMA环形接收+IDLE判帧；模块默认9600bps，USART2已同步改为9600） */
#define BSP_ESP8266_ENABLE      1   /* ESP8266 WiFi模块AT指令驱动（依赖BSP_UART3） */
#define BSP_IWDG_ENABLE         1   /* 独立看门狗IWDG（寄存器直操作，LSI≈32kHz，超时3秒，防系统跑飞；依赖Core/Src/iwdg.c，由app_watchdog统一喂狗） */
#define BSP_KEY_ENABLE          0   /* 按键驱动（GPIO读取+消抖状态机）——F407VET6无PG端口，独立键停用；板载按键 WK_UP=PA0/KEY0=PE4/KEY1=PE3 */
#define BSP_KEY_MATRIX_ENABLE   1   /* 4x4矩阵键盘驱动（行PE2/PE6/PE7/PE10，列PE8/PE9/PE11/PE12，行扫描+列读取；避开板载KEY0=PE4/KEY1=PE3与Echo=PE5） */
#define BSP_BEEP_ENABLE         1   /* 有源蜂鸣器驱动（PA5，NPN三极管驱动，高电平响） */
#define BSP_LIGHT_SENSOR_ENABLE 0   /* 光敏传感器驱动（ADC3_IN5/PF7，查询式单次转换） */
#define BSP_DHT11_ENABLE        0   /* DHT11温湿度驱动——F407VET6无PG9引脚，且SHT30已提供温湿度，停用 */
#define BSP_FLASH_CONFIG_ENABLE 1   /* 内部Flash配置存储（Sector11@0x080E0000，存告警阈值，上电加载） */
#define BSP_I2C_SOFT_ENABLE     1   /* 共享软件I2C总线驱动（PB6=SCL/PB7=SDA，为SHT30/QMI8658/INA226提供主机时序） */
#define BSP_SHT30_ENABLE        1   /* SHT30温湿度传感器驱动（软件I2C，7位地址0x44，ADDR接GND；依赖BSP_I2C_SOFT_ENABLE+BSP_DELAY_ENABLE） */
#define BSP_QMI8658_ENABLE      1   /* QMI8658六轴IMU驱动（软件I2C，7位地址0x6B，本模块AD0板上固定接地；依赖BSP_I2C_SOFT_ENABLE） */
#define BSP_INA226_ENABLE       1   /* INA226电源监测驱动（软件I2C，7位地址0x40，A0/A1接GND；采样电阻0.01Ω R010；依赖BSP_I2C_SOFT_ENABLE） */
#define BSP_RELAY_ENABLE        1   /* 继电器驱动（PA4推挽输出，高电平吸合；COM/NO串接电机电源回路） */
#define BSP_OLED_ENABLE         1   /* OLED驱动（SSD1306，PD6=SCL/PD7=SDA 软件I2C） */
#define BSP_AT24C02_ENABLE      0   /* I2C EEPROM驱动——新核心板无AT24C02芯片，停用 */
#define BSP_W25Q128_ENABLE      0   /* SPI Flash W25Q128——新板W25Q128为板载(SPI1)，工程不使用，停用 */
#define BSP_HC_SR04_ENABLE      0
#define BSP_SD_ENABLE           1   /* SD/TF card driver: SDIO 4bit PC8-PC12+PD2, FatFs diskio */   /* HC-SR04超声波测距驱动（PC11=Trig, PE5=Echo，轮询测量） */
/* ========== PLAT 平台抽象层模块开关（对齐 v2.2a 五层架构，03_plat） ========== */
#define PLAT_OBJ_ENABLE          1   /* 对象模型（plat_obj.h）：plat_dev_t/plat_svc_t + 统一生命周期状态 */
#define PLAT_DEVMGR_ENABLE       1   /* 设备管理器（plat_devmgr）：注册/查找/批量生命周期驱动（依赖PLAT_OBJ_ENABLE） */
#define PLAT_SVCMGR_ENABLE       1   /* 服务管理器（plat_svcmgr）：服务注册/启停+依赖注入校验（依赖PLAT_OBJ_ENABLE） */
#define PLAT_BRDMGR_ENABLE       1   /* 板级管理器（plat_brdmgr）：板级资源表→批量注册设备（依赖PLAT_DEVMGR_ENABLE） */
#define PLAT_CONN_ENABLE         1   /* 连接器抽象（plat_conn）：连接器对象+注册表+统一状态机（bit0=链路 bit1=传输 bit2=应用） */
#define PLAT_OLED_ENABLE         1   /* OLED 设备对象化封装（plat_oled：g_oled_dev + 便捷显示接口，依赖BSP_OLED_ENABLE） */
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
/* 蓝牙透传任务依赖 BT24 BSP 驱动 */
#if APP_BT24_ENABLE && !BSP_BT24_ENABLE
#error "依赖缺失：APP_BT24_ENABLE 依赖 BSP_BT24_ENABLE=1"
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
#if APP_SD_ENABLE && !BSP_SD_ENABLE
#error "APP_SD_ENABLE requires BSP_SD_ENABLE=1"
#endif
#if APP_OFFLINE_ENABLE && !APP_DIAG_ENABLE
#error "依赖缺失：APP_OFFLINE_ENABLE 依赖 APP_DIAG_ENABLE=1"
#endif
/* 阈值配置依赖内部Flash存储 */
#if APP_CONFIG_ENABLE && !BSP_FLASH_CONFIG_ENABLE
#error "依赖缺失：APP_CONFIG_ENABLE 依赖 BSP_FLASH_CONFIG_ENABLE=1"
#endif
/* 多任务看门狗依赖IWDG驱动 */
#if APP_WATCHDOG_ENABLE && !BSP_IWDG_ENABLE
#error "依赖缺失：APP_WATCHDOG_ENABLE 依赖 BSP_IWDG_ENABLE=1"
#endif
/* 平台层依赖校验（对齐 v2.2a 五层架构） */
#if PLAT_DEVMGR_ENABLE && !PLAT_OBJ_ENABLE
#error "依赖缺失：PLAT_DEVMGR_ENABLE 依赖 PLAT_OBJ_ENABLE=1"
#endif
#if PLAT_SVCMGR_ENABLE && !PLAT_OBJ_ENABLE
#error "依赖缺失：PLAT_SVCMGR_ENABLE 依赖 PLAT_OBJ_ENABLE=1"
#endif
#if PLAT_BRDMGR_ENABLE && !PLAT_DEVMGR_ENABLE
#error "依赖缺失：PLAT_BRDMGR_ENABLE 依赖 PLAT_DEVMGR_ENABLE=1"
#endif
#if PLAT_OLED_ENABLE && !BSP_OLED_ENABLE
#error "依赖缺失：PLAT_OLED_ENABLE 依赖 BSP_OLED_ENABLE=1"
#endif
#if APP_CONN_ENGINE_ENABLE && !(PLAT_CONN_ENABLE && APP_MQTT_ENABLE)
#error "依赖缺失：APP_CONN_ENGINE_ENABLE 依赖 PLAT_CONN_ENABLE=1 与 APP_MQTT_ENABLE=1"
#endif
#if APP_PLAT_TASK_ENABLE && !(PLAT_DEVMGR_ENABLE && APP_CONN_ENGINE_ENABLE)
#error "依赖缺失：APP_PLAT_TASK_ENABLE 依赖 PLAT_DEVMGR_ENABLE=1 与 APP_CONN_ENGINE_ENABLE=1"
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
#define APP_CONN_ENGINE_ENABLE  1   /* 连接引擎（app_conn_engine）：把MQTT连接状态注册为plat_conn连接器并周期刷新（依赖PLAT_CONN_ENABLE+APP_MQTT_ENABLE） */
#define APP_BT24_ENABLE         1   /* 蓝牙透传任务（BT24Task）：连接后周期上报传感器快照JSON + 解析小程序下行命令（QUERY/RELAY/LED/PING/BEEP），依赖BSP_BT24_ENABLE，与MQTT共用业务入口 */
#define APP_PLAT_TASK_ENABLE    1   /* 平台服务任务（PlatSvcTask）：周期plat_devmgr_process_all+连接器状态刷新（依赖PLAT_DEVMGR_ENABLE） */
#define APP_WATCHDOG_ENABLE     1   /* 多任务心跳看门狗任务（所有关键任务心跳正常才喂狗，任务卡死自动复位；依赖BSP_IWDG_ENABLE=1，移植自v2.2a） */
#define APP_MODBUS_ENABLE       0   /* Modbus主从一体任务——新板PB1被LCD背光上拉(R13 15K)占用，毕设主线为MQTT云，停用 */
#define APP_EEPROM_ENABLE       0   /* EEPROM配置存取模块（依赖BSP_AT24C02_ENABLE）——新板无AT24C02，停用 */
#define APP_HC_SR04_ENABLE      0
#define APP_SD_ENABLE           1   /* SD data storage task: periodic diag snapshot -> CSV (depends BSP_SD_ENABLE=1) */

/* 串口1(UART1)调试打印过滤：1=只输出 [SD] 打印（调 SD 卡用，其余模块静默）；0=默认集合(MQTT/WiFi/ESP8266/BT24/SD/ALARM/INA226/I2C-Scan) */
#define BSP_UART1_SD_ONLY       0   /* 超声波采集任务（周期测量+串口打印+协议上报+可选LED控制，受APP_TASKS_ENABLE约束，依赖BSP_HC_SR04_ENABLE=1） */

/* ========== 调试开关 ========== */
#define APP_HEX_CMD_DEBUG       0   /* hex帧调试打印：收到/发送hex帧时以十六进制文本打印到串口1（调试用，正式版关闭避免干扰二进制通信） */

/* ========== MQTT/ESP8266 module printf switches (merged from v2.2a) ========== */
#define BSP_ESP8266_UART1_PRINTF_ENABLE  1   /* BSP ESP8266 AT command printf */
#define APP_MQTT_UART1_PRINTF_ENABLE     1   /* APP MQTT cloud task printf */
#define APP_BT24_UART1_PRINTF_ENABLE     1   /* APP BT24 bluetooth task printf */
#define PROTOCOL_MQTT_UART1_PRINTF_ENABLE 1  /* protocol layer mqtt_port printf */
#define APP_WATCHDOG_UART1_PRINTF_ENABLE 1   /* APP watchdog task printf */

/* ========== 各模块 LED 控制独立开关 ==========
 *  仅控制对应模块是否执行 LED 动作，不影响 LED 驱动本身（BSP_LED_ENABLE）。
 *  关闭后该模块仍正常运行（采集/打印/扫描），只是不操作 LED。
 *  需同时满足 BSP_LED_ENABLE=1 才生效。
 */
#define APP_KEY_LED_ENABLE      0   /* 按键控LED（KEY0→LED1亮, KEY1→LED1灭, KEY2→LED2亮, KEY3→LED2灭） */
#define APP_UART1_LED_ENABLE    0   /* 串口1指令控LED——随协议停用 */
#define APP_UART3_LED_ENABLE    1   /* 串口3指令控LED（LED1ON/LED1OFF/LED2ON/LED2OFF/ALLON/ALLOFF，需先禁用ESP8266） */
#define APP_WIFI_LED_ENABLE     1   /* WiFi远程控LED（TCP服务器端发送LED1ON等指令，经ESP8266+IPD解析后执行）；
  仅 APP_WIFI_ENABLE=1 时生效（当前 APP_WIFI_ENABLE=0，MQTT 模式请用云端 LED_Control 命令） */
#define APP_LIGHT_LED_ENABLE    0   /* 光敏传感器控LED（暗光→双灯亮, 中等光→LED2亮, 强光→双灯灭） */
#define APP_DHT11_LED_ENABLE    0   /* 温湿度传感器控LED（高温→LED1+LED2亮, 高湿→LED3+LED4亮） */
#define APP_HC_SR04_LED_ENABLE  0   /* 超声波控LED（距离小于阈值→LED1亮，障碍物报警） */



#endif /* MODULE_CFG_H */
