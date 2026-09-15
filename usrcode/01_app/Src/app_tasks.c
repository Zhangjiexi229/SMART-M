/**
 ******************************************************************************
 * @file    app_tasks.c
 * @brief   应用任务管理 — 集中定义所有 FreeRTOS 任务与对象
 *  当前任务列表：
 *    Uart1RelayTask — UART1 数据转发：BSP环形缓冲 -> FreeRTOS流缓冲
 *    Cmd1Task       — UART1 指令任务：流缓冲 -> Protocol解析 -> LED控制 -> 应答
 *    Uart3RelayTask — UART3 数据转发：BSP环形缓冲 -> FreeRTOS流缓冲
 *    Cmd3Task       — UART3 指令任务：流缓冲 -> Protocol解析 -> LED控制 -> 应答
 *    KeyScanTask   — 按键扫描任务：周期扫描消抖 + 按键事件映射 BEEP/LED 动作
 *    LightSensorTask — 光敏传感器任务：周期 ADC 采集 + 串口打印 + LED 分级控制 + OLED 显示
 *    DHT11Task     — DHT11 温湿度任务：周期单总线采集 + 串口打印 + OLED 显示 + LED 联动
 *    Sht30Task     — SHT30 温湿度任务：周期软件I2C采集 + 串口打印 + 统一快照（MQTT上报）
 *    Qmi8658Task   — QMI8658 六轴IMU任务：周期采集 + 姿态解算 + 串口打印 + 统一快照
 *    Ina226Task    — INA226 电源监测任务：周期采集 + 过流自动断电保护 + 串口打印 + 统一快照
 *    WifiTask      — WiFi 上报任务：ESP8266 连路由器 + TCP 上报温湿度
 *    Bt24Task      — BT24 蓝牙透传任务：连接后周期上报快照JSON + 解析小程序命令（QUERY/RELAY/LED/PING/BEEP）
 *    （可选）LedBlinkTask / LED2_BlinkTask — LED 固定周期闪烁（默认关闭）
 *
 *  条件编译（module_cfg.h）：
 *    APP_UART1_CMD_ENABLE  控制串口1指令相关任务与流缓冲
 *    APP_UART3_CMD_ENABLE  控制串口3指令相关任务与流缓冲
 *    LED_BLINK_TASK_ENABLE 控制LED闪烁任务（与串口/按键控制LED冲突，默认置0）
 *    BSP_BEEP_ENABLE      控制蜂鸣器驱动与上电自检
 *    APP_LIGHT_SENSOR_ENABLE 控制光敏传感器任务（依赖 BSP_LIGHT_SENSOR_ENABLE=1）
 *    APP_DHT11_ENABLE     控制DHT11温湿度任务（依赖 BSP_DHT11_ENABLE=1）
 *    APP_OLED_ENABLE      控制OLED显示应用（依赖 BSP_OLED_ENABLE=1）
 *    APP_WIFI_ENABLE      控制WiFi上报任务（依赖 BSP_ESP8266_ENABLE=1）
 *  集成点：
 *    APP_Init()              -> main.c USER CODE 2
 *    APP_TASKS_CreateObjects() -> freertos.c RTOS_QUEUES
 *    APP_TASKS_CreateTasks()   -> freertos.c RTOS_THREADS
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_TASKS_ENABLE

#include "app_tasks.h"
#include "app_uart.h"
#include "app_cmd.h"
#include "app_key.h"
#if APP_SENSOR_ENABLE
#include "app_sensor.h"
#endif
#if APP_LIGHT_SENSOR_ENABLE
#include "app_light_sensor.h"
#endif
#if APP_DHT11_ENABLE
#include "app_dht11.h"
#endif
#if APP_SHT30_ENABLE
#include "app_sht30.h"
#endif
#if APP_QMI8658_ENABLE
#include "app_qmi8658.h"
#endif
#if APP_INA226_ENABLE
#include "app_ina226.h"
#endif
#if APP_RELAY_ENABLE
#include "app_relay.h"
#endif
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
#include "app_hc_sr04.h"
#endif
#if APP_WIFI_ENABLE
#include "app_wifi.h"
#endif
#if APP_MQTT_ENABLE
#include "app_mqtt.h"
#endif
#if APP_MODBUS_ENABLE
#include "app_modbus.h"
#endif
#if APP_OLED_ENABLE && BSP_OLED_ENABLE
#include "app_oled.h"
#endif
#if APP_ALARM_ENABLE && APP_TASKS_ENABLE
#include "app_alarm.h"
#endif
#if APP_KEY_MATRIX_ENABLE && BSP_KEY_MATRIX_ENABLE
#include "app_key_matrix.h"
#endif
#if APP_CONFIG_ENABLE
#include "app_config.h"
#endif
#if APP_DIAG_ENABLE
#include "app_diag.h"
#endif
#if APP_OFFLINE_ENABLE
#include "app_offline.h"
#endif
#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
#include "app_eeprom.h"
#endif
#if BSP_W25Q128_ENABLE
#include "bsp_w25q128.h"
#endif
#if APP_SD_ENABLE && BSP_SD_ENABLE
#include "app_sd.h"

#endif
#if BSP_KEY_MATRIX_ENABLE
#include "bsp_key_matrix.h"
#endif

#include "bsp_uart.h"
#if BSP_LED_ENABLE
#include "bsp_led.h"
#endif
#if BSP_DELAY_ENABLE
#include "bsp_delay.h"
#endif
#if BSP_LIGHT_SENSOR_ENABLE
#include "bsp_light_sensor.h"
#endif
#if BSP_DHT11_ENABLE
#include "bsp_dht11.h"
#endif
#if BSP_I2C_SOFT_ENABLE
#include "bsp_i2c_soft.h"
#endif
#if BSP_SHT30_ENABLE
#include "bsp_sht30.h"
#endif
#if BSP_QMI8658_ENABLE
#include "bsp_qmi8658.h"
#endif
#if BSP_INA226_ENABLE
#include "bsp_ina226.h"
#endif
#if BSP_RELAY_ENABLE
#include "bsp_relay.h"
#endif
#if BSP_HC_SR04_ENABLE
#include "bsp_hc_sr04.h"
#endif
#if BSP_ESP8266_ENABLE
#include "bsp_esp8266.h"
#endif
#if BSP_BEEP_ENABLE
#include "bsp_beep.h"
#endif
#if APP_WATCHDOG_ENABLE
#include "app_watchdog.h"
#endif
#if APP_CONN_ENGINE_ENABLE && PLAT_CONN_ENABLE
#include "app_conn_engine.h"
#endif
#if APP_BT24_ENABLE && BSP_BT24_ENABLE
#include "app_bt24.h"
#endif
#if PLAT_BRDMGR_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE
#include "plat_brd.h"
#include "plat_mgr.h"
#endif
#if BSP_IWDG_ENABLE
#include "iwdg.h"
#endif
#include "main.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"

/* ==========================================================================
 *  串口1指令链路（Uart1RelayTask + Cmd1Task + 流缓冲）
 * ========================================================================== */
#if APP_UART1_CMD_ENABLE
#define UART1_STREAM_BUF_SIZE   256   /* FreeRTOS 流缓冲大小（字节） */

static StreamBufferHandle_t s_uart1_rx_stream;   /* 串口1接收流缓冲 */
static osThreadId_t         s_uart1RelayTaskHandle;
static osThreadId_t         s_cmd1TaskHandle;

static const osThreadAttr_t s_uart1RelayTask_attr = {
    .name       = "Uart1RelayTask",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
static const osThreadAttr_t s_cmd1Task_attr = {
    .name       = "Cmd1Task",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_UART1_CMD_ENABLE */

/* ==========================================================================
 *  串口3指令链路（Uart3RelayTask + Cmd3Task + 流缓冲）
 * ========================================================================== */
#if APP_UART3_CMD_ENABLE
#define UART3_STREAM_BUF_SIZE   256   /* FreeRTOS 流缓冲大小（字节） */

static StreamBufferHandle_t s_uart3_rx_stream;   /* 串口3接收流缓冲 */
static osThreadId_t         s_uart3RelayTaskHandle;
static osThreadId_t         s_cmd3TaskHandle;

static const osThreadAttr_t s_uart3RelayTask_attr = {
    .name       = "Uart3RelayTask",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
static const osThreadAttr_t s_cmd3Task_attr = {
    .name       = "Cmd3Task",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_UART3_CMD_ENABLE */

/* ==========================================================================
 *  LED闪烁任务（可选，默认关闭）
 * ========================================================================== */
#if LED_BLINK_TASK_ENABLE && BSP_LED_ENABLE
#define LED_BLINK_PERIOD_MS    500   /* LED 翻转周期(ms)，500ms=1Hz 闪烁 */
#define LED2_BLINK_PERIOD_MS   500

static osThreadId_t ledBlinkTaskHandle;
static osThreadId_t led2BlinkTaskHandle;

static const osThreadAttr_t ledBlinkTask_attr = {
    .name       = "LedBlinkTask",
    .stack_size = 128 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
static const osThreadAttr_t led2BlinkTask_attr = {
    .name       = "LED2_BlinkTask",
    .stack_size = 128 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};

static void LedBlinkTask_Entry(void *argument);
static void LED2_BlinkTask_Entry(void *argument);
#endif /* LED_BLINK_TASK_ENABLE */

/* ==========================================================================
 *  按键扫描任务
 * ========================================================================== */
#if APP_KEY_ENABLE
static osThreadId_t s_keyTaskHandle;

static const osThreadAttr_t s_keyTask_attr = {
    .name       = "KeyScanTask",
    .stack_size = 128 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_KEY_ENABLE */

/* ==========================================================================
 *  光敏传感器任务
 * ========================================================================== */
#if APP_LIGHT_SENSOR_ENABLE && BSP_LIGHT_SENSOR_ENABLE
static osThreadId_t s_lightSensorTaskHandle;

static const osThreadAttr_t s_lightSensorTask_attr = {
    .name       = "LightSensorTask",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_LIGHT_SENSOR_ENABLE && BSP_LIGHT_SENSOR_ENABLE */

/* ==========================================================================
 *  DHT11温湿度传感器任务
 * ========================================================================== */
#if APP_DHT11_ENABLE && BSP_DHT11_ENABLE
static osThreadId_t s_dht11TaskHandle;

static const osThreadAttr_t s_dht11Task_attr = {
    .name       = "DHT11Task",
    .stack_size = 512 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_DHT11_ENABLE && BSP_DHT11_ENABLE */

/* ==========================================================================
 *  SHT30温湿度传感器任务
 * ========================================================================== */
#if APP_SHT30_ENABLE && BSP_SHT30_ENABLE
static osThreadId_t s_sht30TaskHandle;

static const osThreadAttr_t s_sht30Task_attr = {
    .name       = "Sht30Task",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_SHT30_ENABLE && BSP_SHT30_ENABLE */

/* ==========================================================================
 *  QMI8658六轴IMU任务
 * ========================================================================== */
#if APP_QMI8658_ENABLE && BSP_QMI8658_ENABLE
static osThreadId_t s_qmi8658TaskHandle;

static const osThreadAttr_t s_qmi8658Task_attr = {
    .name       = "Qmi8658Task",
    .stack_size = 512 * 4,   /* 姿态解算使用浮点库（atan2f/sqrtf），栈需放大 */
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_QMI8658_ENABLE && BSP_QMI8658_ENABLE */

/* ==========================================================================
 *  INA226电源监测任务
 * ========================================================================== */
#if APP_INA226_ENABLE && BSP_INA226_ENABLE
static osThreadId_t s_ina226TaskHandle;

/* ==========================================================================
 *  SD 数据存储任务
 * ========================================================================== */
#if APP_SD_ENABLE && BSP_SD_ENABLE
static osThreadId_t s_sdTaskHandle;

static const osThreadAttr_t s_sdTask_attr = {
    .name       = "SdLogTask",
    .stack_size = 1024 * 12,   /* FatFs + 打印三连 vsnprintf，8KB 仍溢出（pre-delay 打印踩栈），加至 12KB */
    .priority   = (osPriority_t)osPriorityAboveNormal,   /* 原 Normal 与 WiFi/MQTT/传感器同优先级时间片轮转 → SD 轮询读被抢占超 1.28ms 致 RX FIFO 溢出(err=0x20)；提至 AboveNormal 保证排空窗口 */
};
#endif /* APP_SD_ENABLE && BSP_SD_ENABLE */

static const osThreadAttr_t s_ina226Task_attr = {
    .name       = "Ina226Task",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_INA226_ENABLE && BSP_INA226_ENABLE */

/* ==========================================================================
 *  串口日志汇总任务（OLED 故障期间的唯一输出通道，每 2 秒打印一行系统状态）
 *  输出：温度/湿度/振动/母线电压/电流/功率 + 三传感器有效标志 + 告警状态
 * ========================================================================== */
static void APP_SerialLog_Task(void *argument)
{
    APP_Diag_Snapshot_t snap;
    (void)argument;
    for (;;) {
        APP_DIAG_GetSnapshot(&snap);
        /* 暂时屏蔽SYS打印，只留MQTT */
        // int temp_x10 = (int)(snap.temperature * 10.0f);
        // ...
        // BSP_UART1_Printf(...);
        osDelay(2000U);
    }
}

static const osThreadAttr_t s_serialLogTask_attr = {
    .name       = "SerialLog",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,   /* 从 Low 提到 Normal，避免被其他任务饿死 */
};


/* ==========================================================================
 *  超声波测距任务
 * ========================================================================== */
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
static osThreadId_t s_ultrasonicTaskHandle;

static const osThreadAttr_t s_ultrasonicTask_attr = {
    .name       = "UltrasonicTask",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE */

/* ==========================================================================
 *  WiFi上报任务
 * ========================================================================== */
#if APP_WIFI_ENABLE && BSP_ESP8266_ENABLE
static osThreadId_t s_wifiTaskHandle;

static const osThreadAttr_t s_wifiTask_attr = {
    .name       = "WifiTask",
    .stack_size = WIFI_TASK_STACK_SIZE,
    .priority   = (osPriority_t)WIFI_TASK_PRIORITY,
};
#endif /* APP_WIFI_ENABLE && BSP_ESP8266_ENABLE */

/* ==========================================================================
 *  MQTT上云任务
 * ========================================================================== */
#if APP_MQTT_ENABLE && BSP_ESP8266_ENABLE
static osThreadId_t s_mqttTaskHandle;

static const osThreadAttr_t s_mqttTask_attr = {
    .name       = "MqttTask",
    .stack_size = MQTT_TASK_STACK_SIZE,
    .priority   = (osPriority_t)MQTT_TASK_PRIORITY,
};
#endif /* APP_MQTT_ENABLE && BSP_ESP8266_ENABLE */

/* ==========================================================================
 *  BT24 蓝牙透传任务（连接后周期上报 + 小程序下行命令）
 * ========================================================================== */
#if APP_BT24_ENABLE && BSP_BT24_ENABLE
static osThreadId_t s_bt24TaskHandle;

static const osThreadAttr_t s_bt24Task_attr = {
    .name       = "Bt24Task",
    .stack_size = 512 * 4,   /* snprintf 浮点遥测帧 + JSON解析，栈需放大 */
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_BT24_ENABLE && BSP_BT24_ENABLE */

/* ==========================================================================
 *  看门狗监控任务（多任务心跳，所有关键任务心跳正常才喂狗）
 * ========================================================================== */
#if APP_WATCHDOG_ENABLE
static osThreadId_t s_watchdogTaskHandle;

static const osThreadAttr_t s_watchdogTask_attr = {
    .name       = "WatchdogTask",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityAboveNormal,
};
#endif /* APP_WATCHDOG_ENABLE */

/* ==========================================================================
 *  平台服务任务（PlatSvcTask）：周期驱动设备处理 + 连接器状态刷新
 * ========================================================================== */
#if APP_PLAT_TASK_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE
static osThreadId_t s_platTaskHandle;

static void APP_PlatSvcTask(void *argument)
{
    (void)argument;
    for (;;) {
        (void)plat_devmgr_process_all();   /* 统一驱动设备周期处理 */
#if APP_CONN_ENGINE_ENABLE && PLAT_CONN_ENABLE
        app_conn_engine_process();         /* 刷新 MQTT 连接器状态 */
#endif
        osDelay(1000U);
    }
}

static const osThreadAttr_t s_platTask_attr = {
    .name       = "PlatSvcTask",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityLow,
};
#endif /* APP_PLAT_TASK_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE */

/* ==========================================================================
 *  OLED显示任务（毕设：三页面渲染 + 告警反白闪烁）
 * ========================================================================== */
#if APP_OLED_ENABLE && APP_TASKS_ENABLE && BSP_OLED_ENABLE
static osThreadId_t s_oledTaskHandle;

static const osThreadAttr_t s_oledTask_attr = {
    .name       = "DisplayTask",
    .stack_size = 256 * 4,   /* snprintf %f 浮点格式化栈开销大，512B 偏紧，加大到 1KB */
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_OLED_ENABLE && APP_TASKS_ENABLE && BSP_OLED_ENABLE */

/* ==========================================================================
 *  告警联动任务（毕设：阈值告警 + 继电器断电保护 + 蜂鸣器 + 故障诊断）
 * ========================================================================== */
#if APP_ALARM_ENABLE && APP_TASKS_ENABLE
static osThreadId_t s_alarmTaskHandle;

static const osThreadAttr_t s_alarmTask_attr = {
    .name       = "AlarmTask",
    .stack_size = 512 * 4,   /* 每秒做振动特征计算（sqrtf等浮点），栈需放大 */
    .priority   = (osPriority_t)osPriorityAboveNormal,
};
#endif /* APP_ALARM_ENABLE && APP_TASKS_ENABLE */

/* ==========================================================================
 *  4x4矩阵键盘任务（毕设：消抖 + 页面切换/继电器/蜂鸣器/阈值设置）
 * ========================================================================== */
#if APP_KEY_MATRIX_ENABLE && BSP_KEY_MATRIX_ENABLE
static osThreadId_t s_keyMatrixTaskHandle;

static const osThreadAttr_t s_keyMatrixTask_attr = {
    .name       = "KeyMatrixTask",
    .stack_size = 512 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
#endif /* APP_KEY_MATRIX_ENABLE && BSP_KEY_MATRIX_ENABLE */

/* ==========================================================================
 *  Modbus 主从一体任务
 * ========================================================================== */
#if APP_MODBUS_ENABLE
static osThreadId_t s_modbusSlaveTaskHandle;
static osThreadId_t s_modbusMasterTaskHandle;

static const osThreadAttr_t s_modbusSlaveTask_attr = {
    .name       = "MbSlaveTask",
    .stack_size = 1024 * 3,
    .priority   = osPriorityAboveNormal,
};
static const osThreadAttr_t s_modbusMasterTask_attr = {
    .name       = "MbMasterTask",
    .stack_size = 1024 * 3,
    .priority   = osPriorityNormal,
};
#endif /* APP_MODBUS_ENABLE */

/* ==========================================================================
 *  公共API
 * ========================================================================== */

/**
 * @brief  应用层初始化 — BSP 硬件初始化
 * @note   在任务调度器启动前调用（main.c USER CODE 2 */
void APP_Init(void)
{
#if BSP_DELAY_ENABLE
    BSP_Delay_Init();   /* 启动 TIM2 自由计数器（全工程微秒毫秒延时基础） */
#endif

#if BSP_LED_ENABLE
    /* 启动自检（提前到最前）：LED1闪烁3次，确认固件已运行、LED引脚正常。
       无论后续外设初始化结果如何，上电必闪3次，便于无串口时快速判断程序是否在跑 */
    {
        uint8_t i;
        for (i = 0U; i < 3U; i++) {
            BSP_LED_On(BSP_LED1);
            HAL_Delay(120U);
            BSP_LED_Off(BSP_LED1);
            HAL_Delay(120U);
        }
    }
#endif

#if BSP_UART1_ENABLE
    BSP_UART1_Init();   /* 启动UART1 DMA接收 + IDLE判帧 */
#endif

#if BSP_UART3_ENABLE
    BSP_UART3_Init();   /* 启动UART3 DMA接收 + IDLE判帧（ESP8266通信口） */
#endif

#if BSP_ESP8266_ENABLE
    BSP_ESP8266_Init(); /* ESP8266 驱动初始化（清空UART3接收，等待模块上电稳定） */
#endif

#if APP_BT24_ENABLE && BSP_BT24_ENABLE
    APP_BT24_Init();    /* DX-BT24 蓝牙透传（USART2 DMA接收+IDLE判帧）；AT+NOTI1在任务内延时配置 */
#endif

#if APP_MODBUS_ENABLE
    APP_Modbus_RS485_GPIO_Init();  /* RS485 DE/RE 引脚初始化 (PB0=主站, PB1=从站) */
#endif

#if BSP_LIGHT_SENSOR_ENABLE
    BSP_LightSensor_Init();   /* 光敏传感器ADC预热 */
#endif

#if BSP_DHT11_ENABLE
    BSP_DHT11_Init();   /* DHT11 驱动初始化（GPIO 已由 MX 配置）*/
#endif

#if BSP_I2C_SOFT_ENABLE
    BSP_I2C_Soft_Init();    /* 共享软件I2C总线（PB6=SCL/PB7=SDA），SHT30/QMI8658/INA226前置 */
#endif

#if APP_OFFLINE_ENABLE
    APP_OFFLINE_Init();     /* 断网补传环形缓冲清零 */
#endif

#if APP_CONFIG_ENABLE
    APP_CONFIG_Init();      /* 阈值配置加载（内部Flash，无有效数据则用默认值） */
#endif

#if APP_ALARM_ENABLE
    APP_ALARM_Init();       /* 告警状态机清零 + 故障诊断初始化 */
#endif

#if BSP_SHT30_ENABLE
    if (BSP_SHT30_Init() != 0U) {
        BSP_UART1_Printf("[Init] SHT30 init FAILED (addr=0x%02X, check I2C wiring)\r\n",
                         (unsigned)SHT30_I2C_ADDR);
    } else {
        BSP_UART1_Printf("[Init] SHT30 init OK (addr=0x%02X)\r\n", (unsigned)SHT30_I2C_ADDR);
    }
#endif

#if BSP_QMI8658_ENABLE
    if (BSP_QMI8658_Init() != 0U) {
        BSP_UART1_Printf("[Init] QMI8658 init FAILED (addr=0x%02X, check I2C wiring)\r\n",
                         (unsigned)QMI8658_I2C_ADDR);
    } else {
        BSP_UART1_Printf("[Init] QMI8658 init OK (addr=0x%02X)\r\n", (unsigned)QMI8658_I2C_ADDR);
    }
#endif

#if BSP_INA226_ENABLE
    {
        uint8_t ina_ret = BSP_INA226_Init();
        if (ina_ret != 0U) {
            BSP_UART1_Printf("[Init] INA226 init FAILED (addr=0x%02X, ret=%u, cfgrb=0x%04X, check I2C wiring)\r\n",
                             (unsigned)INA226_I2C_ADDR, (unsigned)ina_ret,
                             (unsigned)BSP_INA226_LastCfgReadback);
        } else {
            BSP_UART1_Printf("[Init] INA226 init OK (addr=0x%02X)\r\n", (unsigned)INA226_I2C_ADDR);
        }
    }
#endif

#if BSP_I2C_SOFT_ENABLE
    /* ---- I2C 总线1 地址扫描（诊断用）：探测 0x30~0x77，打印全部在线设备 ---- */
    {
        uint16_t scn_addr;
        uint16_t scn_found = 0U;
        BSP_UART1_Printf("[I2C-Scan] Bus1 (PB6=SCL/PB7=SDA) probing 0x30..0x77...\r\n");
        for (scn_addr = 0x30U; scn_addr <= 0x77U; scn_addr++) {
            if (BSP_I2C_Soft_Probe((uint8_t)scn_addr) == 0U) {
                BSP_UART1_Printf("[I2C-Scan]   found 0x%02X\r\n", (unsigned)scn_addr);
                scn_found++;
            }
        }
        BSP_UART1_Printf("[I2C-Scan] done: %u device(s) online\r\n", (unsigned)scn_found);
    }
#endif

#if BSP_RELAY_ENABLE
    BSP_UART1_Printf("[Init] >> RELAY\r\n");
    BSP_RELAY_Init();   /* 继电器GPIO初始化（PA4），初始断开 */
#if APP_SENSOR_ENABLE
    APP_SENSOR_UpdateRelay(0U, 1U);   /* 初始状态写入统一快照 */
#endif
#endif

#if BSP_HC_SR04_ENABLE
    BSP_HC_SR04_Init(); /* HC-SR04 超声波驱动初始化（Trig=PC11, Echo=PE5） */
#endif

#if APP_OLED_ENABLE && BSP_OLED_ENABLE
    APP_OLED_Init();    /* OLED 硬件初始化 + 清屏 + 显示静态标签 */
#endif

#if BSP_KEY_MATRIX_ENABLE
    BSP_UART1_Printf("[Init] >> KEYMATRIX\r\n");
    BSP_KeyMatrix_Init();   /* 4x4矩阵键盘 GPIO 初始化（行PE2/PE6/PE7/PE10，列PE8/PE9/PE11/PE12） */
#endif

#if BSP_W25Q128_ENABLE
    BSP_W25Q128_Init();
    (void)BSP_W25Q128_SelfTest();
#endif

#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
    /* EEPROM 配置加载（校验CRC，失败则写入默认值） */
    (void)APP_EEPROM_Init();
    /* 重启计数+1并保存 */
    (void)APP_EEPROM_IncrementReboot();
#endif

#if BSP_LED_ENABLE && APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
    /* 从 EEPROM 恢复上次 LED 状态（掉电保持）*/
    {
        const app_config_t *cfg = APP_EEPROM_GetConfig();
        if (cfg->led_state & 0x01U) BSP_LED_On(BSP_LED1); else BSP_LED_Off(BSP_LED1);
        if (cfg->led_state & 0x02U) BSP_LED_On(BSP_LED2); else BSP_LED_Off(BSP_LED2);
        if (cfg->led_state & 0x04U) BSP_LED_On(BSP_LED3); else BSP_LED_Off(BSP_LED3);
        if (cfg->led_state & 0x08U) BSP_LED_On(BSP_LED4); else BSP_LED_Off(BSP_LED4);
        BSP_UART1_Printf("[Init] LED state restored from EEPROM: 0x%02X\r\n", cfg->led_state);
    }
#endif

#if BSP_BEEP_ENABLE
    /* 启动自检：蜂鸣器短鸣100ms，确认BEEP引脚与三极管驱动电路正常 */
    BSP_UART1_Printf("[Init] >> BEEP (beep skipped, HAL_Delay pre-scheduler)\r\n");
    BSP_BEEP_Off();   /* 开机先关蜂鸣器（低电平触发，初始低电平=响） */
    /* BSP_BEEP_Beep(100U);  // 临时注释：调度器启动前 HAL_Delay 依赖 SysTick，被 FreeRTOS 接管后死循环 */
#endif

#if BSP_IWDG_ENABLE
    /* 独立看门狗启动（调度器启动前的最后一步）：
     * 所有耗时初始化（ESP8266上电2s等待等）已完成，
     * 之后由 app_watchdog 任务（最先创建）周期喂狗 */
    MX_IWDG_Init();
#endif

#if PLAT_BRDMGR_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE
    /* 平台层：注册板级设备表 → 批量 init/start（对象化设备生命周期） */
    (void)plat_brdmgr_init();
    (void)plat_devmgr_init_all();
    (void)plat_devmgr_start_all();
#endif
#if APP_CONN_ENGINE_ENABLE && PLAT_CONN_ENABLE
    /* 注册 MQTT 连接器到 plat_conn（状态由 app_mqtt 维护，PlatSvcTask 周期刷新） */
    (void)app_conn_engine_mqtt_register();
#endif

    BSP_UART1_Printf("[Init] APP_Init done, starting scheduler...\r\n");
}

/**
 * @brief  创建所有 RTOS 对象（流缓冲、队列、信号量、互斥锁） */
void APP_TASKS_CreateObjects(void)
{
#if APP_UART1_CMD_ENABLE
    s_uart1_rx_stream = xStreamBufferCreate(UART1_STREAM_BUF_SIZE, 1);
    if (s_uart1_rx_stream == NULL) {
        Error_Handler();
    }
#endif

#if APP_UART3_CMD_ENABLE
    s_uart3_rx_stream = xStreamBufferCreate(UART3_STREAM_BUF_SIZE, 1);
    if (s_uart3_rx_stream == NULL) {
        Error_Handler();
    }
#endif

#if APP_OLED_ENABLE && BSP_OLED_ENABLE
    APP_OLED_CreateMutex();   /* OLED 写互斥锁（防 DHT11/光敏任务并发写屏） */
#endif
}

/**
 * @brief  创建所有应用任务 */
void APP_TASKS_CreateTasks(void)
{
#if APP_WATCHDOG_ENABLE
    /* Watchdog 任务最先创建：确保优先获得堆内存；创建失败则进入喂狗死循环防复位 */
    s_watchdogTaskHandle = osThreadNew(APP_Watchdog_Task, NULL, &s_watchdogTask_attr);
    if (s_watchdogTaskHandle == NULL) {
        BSP_UART1_Printf("[FATAL] Watchdog task create failed (heap too small?), feeding dog forever\r\n");
        for (;;) {
            MX_IWDG_Refresh();
        }
    }
#endif /* APP_WATCHDOG_ENABLE */

#if APP_PLAT_TASK_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE
    s_platTaskHandle = osThreadNew(APP_PlatSvcTask, NULL, &s_platTask_attr);
    if (s_platTaskHandle == NULL) {
        BSP_UART1_Printf("[FATAL] PlatSvc task create failed\r\n");
    }
#endif /* APP_PLAT_TASK_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE */

#if APP_UART1_CMD_ENABLE
    s_uart1RelayTaskHandle = osThreadNew(APP_UART1_RelayTask, s_uart1_rx_stream, &s_uart1RelayTask_attr);
    s_cmd1TaskHandle       = osThreadNew(APP_CMD1_Task,       s_uart1_rx_stream, &s_cmd1Task_attr);
#endif /* APP_UART1_CMD_ENABLE */

#if APP_UART3_CMD_ENABLE
    s_uart3RelayTaskHandle = osThreadNew(APP_UART3_RelayTask, s_uart3_rx_stream, &s_uart3RelayTask_attr);
    s_cmd3TaskHandle       = osThreadNew(APP_CMD3_Task,       s_uart3_rx_stream, &s_cmd3Task_attr);
#endif /* APP_UART3_CMD_ENABLE */

#if LED_BLINK_TASK_ENABLE && BSP_LED_ENABLE
    ledBlinkTaskHandle = osThreadNew(LedBlinkTask_Entry, NULL, &ledBlinkTask_attr);
    led2BlinkTaskHandle = osThreadNew(LED2_BlinkTask_Entry, NULL, &led2BlinkTask_attr);
#endif /* LED_BLINK_TASK_ENABLE */

#if APP_KEY_ENABLE
    s_keyTaskHandle = osThreadNew(APP_KEY_Task, NULL, &s_keyTask_attr);
#endif /* APP_KEY_ENABLE */

#if APP_LIGHT_SENSOR_ENABLE && BSP_LIGHT_SENSOR_ENABLE
    s_lightSensorTaskHandle = osThreadNew(APP_LightSensor_Task, NULL, &s_lightSensorTask_attr);
#endif /* APP_LIGHT_SENSOR_ENABLE && BSP_LIGHT_SENSOR_ENABLE */

#if APP_DHT11_ENABLE && BSP_DHT11_ENABLE
    s_dht11TaskHandle = osThreadNew(APP_DHT11_Task, NULL, &s_dht11Task_attr);
#endif /* APP_DHT11_ENABLE && BSP_DHT11_ENABLE */

#if APP_SHT30_ENABLE && BSP_SHT30_ENABLE
    s_sht30TaskHandle = osThreadNew(APP_SHT30_Task, NULL, &s_sht30Task_attr);
#if APP_WATCHDOG_ENABLE
    if (s_sht30TaskHandle != NULL) { Watchdog_Register(WDT_TASK_SHT30); }
#endif
#endif /* APP_SHT30_ENABLE && BSP_SHT30_ENABLE */

#if APP_QMI8658_ENABLE && BSP_QMI8658_ENABLE
    s_qmi8658TaskHandle = osThreadNew(APP_QMI8658_Task, NULL, &s_qmi8658Task_attr);
#if APP_WATCHDOG_ENABLE
    if (s_qmi8658TaskHandle != NULL) { Watchdog_Register(WDT_TASK_QMI8658); }
#endif
#endif /* APP_QMI8658_ENABLE && BSP_QMI8658_ENABLE */

#if APP_INA226_ENABLE && BSP_INA226_ENABLE
    s_ina226TaskHandle = osThreadNew(APP_INA226_Task, NULL, &s_ina226Task_attr);
#if APP_WATCHDOG_ENABLE
    if (s_ina226TaskHandle != NULL) { Watchdog_Register(WDT_TASK_INA226); }
#endif
#endif /* APP_INA226_ENABLE && BSP_INA226_ENABLE */

#if APP_SD_ENABLE && BSP_SD_ENABLE
    s_sdTaskHandle = osThreadNew(APP_SD_Task, NULL, &s_sdTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_sdTaskHandle != NULL) { Watchdog_Register(WDT_TASK_SD); }
#endif
    if (s_sdTaskHandle == NULL) {
        /* 任务未创建（多为 FreeRTOS 堆不足），串口可看到诊断 */
        BSP_UART1_Printf("[SD] task create FAIL (free heap low?)\r\n");
    }
#endif /* APP_SD_ENABLE && BSP_SD_ENABLE */

#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE
    s_ultrasonicTaskHandle = osThreadNew(APP_HC_SR04_Task, NULL, &s_ultrasonicTask_attr);
#endif /* APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE */

#if APP_WIFI_ENABLE && BSP_ESP8266_ENABLE
    s_wifiTaskHandle = osThreadNew(APP_WIFI_Task, NULL, &s_wifiTask_attr);
#endif /* APP_WIFI_ENABLE && BSP_ESP8266_ENABLE */

#if APP_MQTT_ENABLE && BSP_ESP8266_ENABLE
    s_mqttTaskHandle = osThreadNew(APP_MQTT_Task, NULL, &s_mqttTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_mqttTaskHandle != NULL) { Watchdog_Register(WDT_TASK_MQTT); }
#endif
#endif /* APP_MQTT_ENABLE && BSP_ESP8266_ENABLE */

#if APP_BT24_ENABLE && BSP_BT24_ENABLE
    s_bt24TaskHandle = osThreadNew(APP_BT24_Task, NULL, &s_bt24Task_attr);
#if APP_WATCHDOG_ENABLE
    if (s_bt24TaskHandle != NULL) { Watchdog_Register(WDT_TASK_BT24); }
#endif
#endif /* APP_BT24_ENABLE && BSP_BT24_ENABLE */

#if APP_OLED_ENABLE && APP_TASKS_ENABLE && BSP_OLED_ENABLE
    s_oledTaskHandle = osThreadNew(APP_OLED_DisplayTask, NULL, &s_oledTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_oledTaskHandle != NULL) { Watchdog_Register(WDT_TASK_OLED); }
#endif
#endif /* APP_OLED_ENABLE && APP_TASKS_ENABLE && BSP_OLED_ENABLE */

    /* 串口日志汇总任务：无条件创建（OLED 故障期间的唯一输出通道） */
    (void)osThreadNew(APP_SerialLog_Task, NULL, &s_serialLogTask_attr);

#if APP_ALARM_ENABLE && APP_TASKS_ENABLE
    s_alarmTaskHandle = osThreadNew(APP_ALARM_Task, NULL, &s_alarmTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_alarmTaskHandle != NULL) { Watchdog_Register(WDT_TASK_ALARM); }
#endif
#endif /* APP_ALARM_ENABLE && APP_TASKS_ENABLE */

#if APP_KEY_MATRIX_ENABLE && BSP_KEY_MATRIX_ENABLE
    s_keyMatrixTaskHandle = osThreadNew(APP_KeyMatrix_Task, NULL, &s_keyMatrixTask_attr);
#if APP_WATCHDOG_ENABLE
    if (s_keyMatrixTaskHandle != NULL) { Watchdog_Register(WDT_TASK_KEY_MATRIX); }
#endif
#endif /* APP_KEY_MATRIX_ENABLE && BSP_KEY_MATRIX_ENABLE */

#if APP_MODBUS_ENABLE
    s_modbusSlaveTaskHandle  = osThreadNew(APP_ModbusSlave_Task,  NULL, &s_modbusSlaveTask_attr);
    s_modbusMasterTaskHandle = osThreadNew(APP_ModbusMaster_Task, NULL, &s_modbusMasterTask_attr);
#endif /* APP_MODBUS_ENABLE */
}

/* ==========================================================================
 *  任务实现
 * ========================================================================== */
#if LED_BLINK_TASK_ENABLE && BSP_LED_ENABLE
/**
 * @brief  LED1 闪烁任务 — 每500ms翻转一次 LED1，1Hz 闪烁）
 * @note   低电平点亮，初始状态由 MX_GPIO_Init 设置为 SET(灭）
 */
static void LedBlinkTask_Entry(void *argument)
{
    (void)argument;
    for (;;) {
        BSP_LED_Toggle(BSP_LED1);
        osDelay(LED_BLINK_PERIOD_MS);
    }
}

/**
 * @brief  LED2 闪烁任务 — 每500ms翻转一次 LED2，1Hz 闪烁） */
static void LED2_BlinkTask_Entry(void *argument)
{
    (void)argument;
    for (;;) {
        BSP_LED_Toggle(BSP_LED2);
        osDelay(LED2_BLINK_PERIOD_MS);
    }
}
#endif /* LED_BLINK_TASK_ENABLE */

/* ==========================================================================
 *  FreeRTOS 钩子函数（栈溢出 / 内存分配失败）
 *  覆盖 cmsis_os2.c 中的 __WEAK 默认空实现；开启条件见 FreeRTOSConfig.h
 *    configCHECK_FOR_STACK_OVERFLOW = 2
 *    configUSE_MALLOC_FAILED_HOOK   = 1
 * ========================================================================== */
#if (configCHECK_FOR_STACK_OVERFLOW > 0)
void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName)
{
    (void)xTask;
    /* 直发串口（绕过 BSP_UART1_Printf 过滤器），栈溢出信息不被 SD_ONLY 过滤掉 */
    BSP_UART1_SendString("[RTOS] *** STACK OVERFLOW in ");
    if (pcTaskName != NULL) {
        BSP_UART1_SendString((const char *)pcTaskName);
    }
    BSP_UART1_SendString(" ***\r\n");
    /* 卡死并闪烁LED1，便于定位（重新上电可恢复） */
    for (;;) {
#if BSP_LED_ENABLE
        BSP_LED_On(BSP_LED1);
        HAL_Delay(100U);
        BSP_LED_Off(BSP_LED1);
        HAL_Delay(100U);
#endif
    }
}
#endif /* configCHECK_FOR_STACK_OVERFLOW */

#if (configUSE_MALLOC_FAILED_HOOK == 1)
void vApplicationMallocFailedHook(void)
{
    BSP_UART1_Printf("[RTOS] *** MALLOC FAILED (heap exhausted) ***\r\n");
    for (;;) {
        /* 卡死，便于通过调试器查看堆使用情况 */
    }
}
#endif /* configUSE_MALLOC_FAILED_HOOK */

#endif /* APP_TASKS_ENABLE */
