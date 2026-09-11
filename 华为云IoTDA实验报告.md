# STM32F407 + ESP8266 + Paho MQTT 接入华为云 IoTDA 实验报告

> 实验状态：已验证通过（设备在线、属性上报正常、命令下发与响应正常）
> 文档版本：2026-09-04

---

## 一、实验概述

本实验基于 STM32F407 开发板，通过 ESP8266 WiFi 模块以 AT 指令方式接入网络，使用 Eclipse Paho MQTT Embedded C 同步客户端库，连接**华为云 IoT 设备接入服务（IoTDA）**，实现：

- **温湿度数据周期上报**：DHT11 传感器数据以华为云物模型 JSON 格式发布到平台，平台自动存储并生成历史趋势
- **云平台远程控制**：通过华为云控制台下发 `LED_Control` 命令，设备解析后控制板载 LED，并回复命令执行结果
- **断线自动重连**：WiFi/TCP/MQTT 任一环节失败后延迟 3 秒从头重连
- **设备自动激活**：设备首次 MQTT 连接成功后，平台状态从"未激活"自动变为"在线"

---

## 二、硬件与软件环境

### 2.1 硬件平台

| 项目 | 内容 |
|---|---|
| MCU | STM32F407（Cortex-M4，168MHz） |
| WiFi 模块 | ESP8266（UART3，PB10=TX，PB11=RX，115200 8N1） |
| 温湿度传感器 | DHT11（PG9 单总线） |
| 板载 LED | LED1=PG14, LED2=PG13, LED3=PG6, LED4=PG11 |
| 调试串口 | USART1（PA9/PA10，115200） |
| 存储 | AT24C02 EEPROM（配置保存）、W25Q128 SPI Flash |

### 2.2 软件平台

| 项目 | 内容 |
|---|---|
| 操作系统 | FreeRTOS（CMSIS-RTOS2 封装） |
| HAL 库 | STM32CubeMX 生成 |
| MQTT 库 | Eclipse Paho MQTT Embedded C（同步客户端） |
| MQTT 协议版本 | MQTT 3.1.1 |
| 云平台 | 华为云 IoT 设备接入 IoTDA（基础版实例） |
| 接入方式 | MQTT 非加密（1883 端口） |
| 构建系统 | CMake + STM32CubeMX |
| 开发工具 | VS Code + CMake 插件 + ST-Link |

### 2.3 云平台规格

| 项目 | 内容 |
|---|---|
| 实例类型 | 基础版（basic instance） |
| 区域 | 华北-北京四（cn-north-4） |
| 免费额度 | 1000 设备注册、10000 条消息/日、10 TPS |
| 接入地址 | `your_mqtt_address.iot-mqtts.cn-north-4.myhuaweicloud.com` |
| 非加密端口 | 1883 |
| 加密端口 | 8883（MQTTS/TLS，本实验未使用） |

---

## 三、华为云 IoTDA 平台配置

### 3.1 开通服务

1. 登录 [华为云控制台](https://console.huaweicloud.com/)
2. 搜索"设备接入"进入 IoTDA 服务
3. 开通**基础版实例**（免费单元 S0）
4. 进入实例控制台

### 3.2 创建产品

左侧"产品" → 创建产品：

| 参数 | 值 |
|---|---|
| 产品名称 | `STM32_Sensor` |
| 协议类型 | **MQTT** |
| 数据格式 | **JSON** |
| 设备类型 | 自定义 `Sensor` |
| 厂商名称 | `Developer` |

### 3.3 定义物模型

产品 → 功能定义 → 添加服务：

**服务 ID：`Sensor`**

添加属性：

| 属性名 | 数据类型 | 取值范围 | 单位 |
|---|---|---|---|
| `temperature` | decimal | -40 ~ 80 | ℃ |
| `humidity` | decimal | 0 ~ 100 | %RH |

添加命令：

| 命令名 | 参数名 | 数据类型 | 说明 |
|---|---|---|---|
| `LED_Control` | `led` | int | 0~15，bit0=LED1, bit1=LED2, bit2=LED3, bit3=LED4 |

### 3.4 注册设备

左侧"设备" → 添加设备：

| 参数 | 值 |
|---|---|
| 所属产品 | `STM32_Sensor` |
| 设备标识码 | `stm32_dev01` |
| 设备名称 | `stm32_dev01` |
| 认证类型 | 密钥（自动生成） |

注册成功后保存：
- **device_id**：`your_device_id`
- **device_secret**：`your_device_secret`

### 3.5 生成 MQTT 鉴权参数

使用华为云官方在线工具：
**https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/**

| 参数 | 选择/填写 |
|---|---|
| 认证类型 | 密钥认证 |
| 密码签名类型 | **不校验时间戳**（嵌入式设备无 RTC，选此项生成一次永久有效） |
| DeviceId | `your_device_id` |
| DeviceSecret | `your_device_secret` |

生成结果：

| MQTT 参数 | 值 |
|---|---|
| ClientId | `your_client_id` |
| Username | `your_device_id` |
| Password | `your_mqtt_password` |

> **关键说明**：Password 不是 device_secret 明文，而是用 device_secret 对 clientId 做 HMAC-SHA256 签名后的 64 位十六进制字符串。直接填 device_secret 会连接失败（rc=-1）。

### 3.6 获取接入地址

控制台"总览" → "接入信息" → 复制 MQTT 接入域名：
`your_mqtt_address.iot-mqtts.cn-north-4.myhuaweicloud.com`

> **非加密接入**：域名保持不变，端口从 8883 改为 1883。**不要**把域名中的 `iot-mqtts` 改成 `iot-mqtt`（该域名不存在，会导致 DNS 解析失败）。

---

## 四、MQTT 配置参数

所有配置集中在 `usrcode/APP/Inc/app_mqtt.h`：

```c
/* --- WiFi --- */
#define MQTT_WIFI_SSID        "spring"
#define MQTT_WIFI_PASSWORD    "mo12345678"

/* --- 华为云 IoTDA 接入参数 --- */
#define MQTT_BROKER_HOST      "your_mqtt_address.iot-mqtts.cn-north-4.myhuaweicloud.com"
#define MQTT_BROKER_PORT      1883U
#define MQTT_CLIENT_ID        "your_client_id"
#define MQTT_USERNAME         "your_device_id"
#define MQTT_PASSWORD         "your_mqtt_password"
#define MQTT_KEEPALIVE_SEC    60U

/* --- 华为云物模型 --- */
#define HUAWEI_DEVICE_ID      "your_device_id"
#define HUAWEI_SERVICE_ID     "Sensor"

/* --- 运行参数 --- */
#define MQTT_QOS                0
#define MQTT_PUBLISH_PERIOD_MS  3000U
#define MQTT_RECONNECT_DELAY_MS 3000U
#define MQTT_YIELD_TIMEOUT_MS   200
#define MQTT_TASK_STACK_SIZE    (1024U * 4U)
#define MQTT_TASK_PRIORITY      osPriorityNormal
```

### 4.1 华为云 MQTT 主题

主题在代码中运行时拼接（`huawei_build_topics()`）：

| 方向 | 主题 | 说明 |
|---|---|---|
| 设备→云（发布） | `$oc/devices/your_device_id/sys/properties/report` | 属性上报 |
| 云→设备（订阅） | `$oc/devices/your_device_id/sys/commands/#` | 命令下发（#通配 request_id） |
| 设备→云（发布） | `$oc/devices/your_device_id/sys/commands/response/request_id={rid}` | 命令响应 |

### 4.2 数据格式

**属性上报（设备→云）**：
```json
{"services":[{"service_id":"Sensor","properties":{"temperature":28.5,"humidity":52.0}}]}
```

**命令下发（云→设备）**：
```json
{"paras":{"led":1},"service_id":"Sensor","command_name":"LED_Control"}
```

**命令响应（设备→云）**：
```json
{"result_code":0,"response_name":"COMMAND_RESPONSE","paras":{"result":"success","led":1}}
```

---

## 五、具体操作流程

### 5.1 控制台操作（10 步）

1. 开通华为云 IoTDA 基础版实例
2. 创建产品（协议=MQTT，数据格式=JSON）
3. 功能定义：添加服务 `Sensor`
4. 添加属性 `temperature`、`humidity`
5. 添加命令 `LED_Control`（参数 `led` int）
6. 添加设备，保存 device_id 和 device_secret
7. 用在线工具生成 ClientId/Username/Password
8. 控制台"总览→接入信息"获取 MQTT 接入域名
9. 将参数填入 `app_mqtt.h`
10. 编译烧录，设备上电后自动激活

### 5.2 代码修改流程

1. 修改 `app_mqtt.h`：填入 WiFi、Broker、鉴权三要素、设备ID、服务ID
2. `app_mqtt.c` 已预置华为云逻辑（物模型 JSON、命令解析、响应回复），无需额外修改
3. 确认 `module_cfg.h` 中 `APP_MQTT_ENABLE=1`、`BSP_ESP8266_ENABLE=1`、`APP_DHT11_ENABLE=1`
4. 确认 `APP_WIFI_ENABLE=0`（与 MQTT 互斥）

### 5.3 编译烧录

```bash
# VS Code 中选择 Debug 配置 → Build
# 生成 pro_v2.1.hex / pro_v2.1.bin
# ST-Link 烧录到开发板
```

### 5.4 运行验证

**串口输出（115200）**：
```
[MQTT] Task started, broker=your_mqtt_address.iot-mqtts.cn-north-4.myhuaweicloud.com:1883
[MQTT] ===== Connecting to Huawei Cloud =====
[MQTT] AT test... AT OK
[MQTT] Joining AP "spring" ... WiFi connected
[MQTT] TCP connecting ... TCP connected
[MQTT] MQTT connected (client=your_client_id)
[MQTT] Subscribed to "$oc/devices/your_device_id/sys/commands/#" (QoS=0)
[MQTT] ===== All connected, start publishing =====
[MQTT] Published: {"services":[{"service_id":"Sensor","properties":{"temperature":28.2,"humidity":52.0}}]}
```

**控制台验证**：
- 设备列表状态：**在线**（绿色圆点）
- 设备→属性页面：temperature、humidity 每 3 秒刷新
- 设备→命令页面：下发 `LED_Control`，参数 `led=1`，LED1 点亮，命令状态"已送达"

---

## 六、增加和修改的文件

### 6.1 新增文件

| 文件路径 | 说明 | 行数 |
|---|---|---|
| `usrcode/APP/Inc/app_mqtt.h` | MQTT 任务头文件：配置宏、API 声明 | ~80 |
| `usrcode/APP/Src/app_mqtt.c` | MQTT 任务实现：连接状态机、属性上报、命令解析、响应回复 | ~560 |
| `usrcode/Protocol/Inc/mqtt_port.h` | Paho 移植层头文件：Timer + Network 类型定义 | ~65 |
| `usrcode/Protocol/Src/mqtt_port.c` | Paho 移植层实现：Timer(HAL tick) + Network(ESP8266 AT) | ~150 |
| `Middlewares/Third_Party/paho/MQTTClient-C/src/MQTTClient.c` | Paho 同步客户端（第三方） | ~600 |
| `Middlewares/Third_Party/paho/MQTTClient-C/src/MQTTClient.h` | Paho 客户端头文件 | ~120 |
| `Middlewares/Third_Party/paho/MQTTPacket/src/*.c` | Paho 报文编解码（6 个源文件） | — |

### 6.2 修改文件

| 文件路径 | 修改内容 |
|---|---|
| `usrcode/Common/Inc/module_cfg.h` | 新增 `APP_MQTT_ENABLE` 开关；新增与 `APP_WIFI_ENABLE` 的互斥 `#error` 检查 |
| `usrcode/APP/Src/app_tasks.c` | 新增 MQTT 任务句柄、属性、创建代码（`osThreadNew(APP_MQTT_Task, ...)`） |
| `CMakeLists.txt` | 新增 Paho MQTT 源文件列表、include 路径、`MQTTCLIENT_PLATFORM_HEADER` 编译宏 |
| `Middlewares/Third_Party/paho/MQTTClient-C/src/MQTTClient.c` | 修复 `enum QoS`（1字节）强转 `int*`（4字节）导致的编译警告，改用临时 `int` 变量中转 |

### 6.3 Paho 库编译裁剪

CMakeLists.txt 中仅编译客户端需要的 7 个文件，排除服务端（`*Server.c`）和工具文件（`MQTTFormat.c`）：

```
MQTTPacket.c              — 通用工具
MQTTConnectClient.c       — CONNECT 报文
MQTTDeserializePublish.c  — PUBLISH 反序列化
MQTTSerializePublish.c    — PUBLISH 序列化
MQTTSubscribeClient.c     — SUBSCRIBE/SUBACK
MQTTUnsubscribeClient.c   — UNSUBSCRIBE/UNSUBACK
MQTTClient.c              — 同步客户端主逻辑
```

---

## 七、项目框架

### 7.1 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│                      APP 应用层                              │
│  app_mqtt.c  app_dht11.c  app_key.c  app_cmd.c  app_eeprom.c│
│  （业务逻辑、FreeRTOS任务、状态机）                           │
├─────────────────────────────────────────────────────────────┤
│                    Protocol 协议层                           │
│  mqtt_port.c  protocol.c  protocol_hex.c                    │
│  （Paho移植层、文本协议、十六进制帧协议）                      │
├─────────────────────────────────────────────────────────────┤
│                      BSP 板级支持包                          │
│  bsp_esp8266.c  bsp_uart.c  bsp_dht11.c  bsp_led.c  ...     │
│  （硬件驱动、AT指令、DMA+IDLE接收）                          │
├─────────────────────────────────────────────────────────────┤
│                      Common 公共层                           │
│  module_cfg.h  ring_buffer.c  common.c                      │
│  （模块开关、环形缓冲、通用工具）                             │
├─────────────────────────────────────────────────────────────┤
│              Core / HAL / FreeRTOS / Paho                   │
│  STM32CubeMX 生成：GPIO/UART/ADC/DMA/TIM + FreeRTOS + Paho  │
└─────────────────────────────────────────────────────────────┘
```

### 7.2 目录结构

```
pro_v2.1/
├── Core/                        # STM32CubeMX 生成（HAL初始化、main.c、freertos.c）
├── Drivers/                     # STM32 HAL 驱动库
├── Middlewares/
│   └── Third_Party/
│       └── paho/                # Paho MQTT Embedded C 第三方库
│           ├── MQTTClient-C/src/
│           └── MQTTPacket/src/
├── usrcode/                     # 用户代码（分层组织）
│   ├── APP/                     # 应用层
│   │   ├── Inc/  app_mqtt.h, app_dht11.h, app_tasks.h ...
│   │   └── Src/  app_mqtt.c, app_dht11.c, app_tasks.c ...
│   ├── BSP/                     # 板级支持包
│   │   ├── Inc/  bsp_esp8266.h, bsp_uart.h, bsp_dht11.h ...
│   │   └── Src/  bsp_esp8266.c, bsp_uart.c, bsp_dht11.c ...
│   ├── Protocol/                # 协议层
│   │   ├── Inc/  mqtt_port.h, protocol.h, protocol_hex.h
│   │   └── Src/  mqtt_port.c, protocol.c, protocol_hex.c
│   └── Common/                  # 公共层
│       ├── Inc/  module_cfg.h, ring_buffer.h, common.h
│       └── Src/  ring_buffer.c, common.c
├── CMakeLists.txt
└── pro.ioc
```

### 7.3 条件编译模块化设计

所有模块通过 `module_cfg.h` 中的宏开关控制，禁用模块不生成任何代码。关键互斥约束：

```c
#if BSP_ESP8266_ENABLE && APP_UART3_CMD_ENABLE
#error "冲突：UART3只能用于ESP8266或串口指令控LED二者之一"
#endif
#if APP_MQTT_ENABLE && APP_WIFI_ENABLE
#error "冲突：MQTT与WiFi上报共用ESP8266，二选一"
#endif
```

---

## 八、程序流程

### 8.1 系统启动流程

```
main()
  ├─ HAL_Init()
  ├─ SystemClock_Config()          // 168MHz
  ├─ MX_GPIO/DMA/USART1/USART3/ADC3/TIM2_Init()
  ├─ osKernelInitialize()
  ├─ MX_FREERTOS_Init()
  │     ├─ APP_Init()              // BSP硬件初始化
  │     │    ├─ BSP_Delay_Init()   // TIM2自由计数器
  │     │    ├─ BSP_UART1_Init()   // DMA+IDLE接收
  │     │    ├─ BSP_UART3_Init()   // ESP8266通信口
  │     │    ├─ BSP_ESP8266_Init()
  │     │    ├─ BSP_DHT11_Init()
  │     │    └─ LED自检闪烁3次
  │     ├─ APP_TASKS_CreateObjects()
  │     └─ APP_TASKS_CreateTasks()
  │           ├─ DHT11Task          // 温湿度采集（5秒周期）
  │           ├─ KeyScanTask        // 按键扫描
  │           ├─ Uart1RelayTask     // UART1数据转发
  │           ├─ Cmd1Task           // UART1指令解析
  │           └─ MqttTask           // MQTT上云任务 ★
  └─ osKernelStart()               // 启动调度器
```

### 8.2 MQTT 任务状态机

```
MqttTask (osDelay 500ms 等待传感器初始化)
    │
    ▼
┌──────────────────────────┐
│  阶段A：未连接            │
│  s_mqtt_status = 0       │
│  mqtt_full_connect()     │◄──────────────┐
└──────┬───────────────────┘               │
       │ 成功                              │ 失败
       ▼                                   │
┌──────────────────────────┐               │
│  阶段B：属性上报          │               │
│  mqtt_publish_once()     │               │
│  (物模型JSON→MQTTPublish) │               │
└──────┬───────────────────┘               │
       │ 成功                              │ 失败
       ▼                                   │
┌──────────────────────────┐               │
│  阶段C：维持连接          │               │
│  while(waited < 3000ms)  │               │
│    MQTTYield(200ms)      │               │
│    mqtt_flush_reply()    │               │
│    waited += 200         │               │
└──────┬───────────────────┘               │
       │ 周期到→回到阶段B                   │ Yield失败
       │                                    │
       └────────────────────────────────────┘
              失败 → MQTTDisconnect → osDelay(3000ms) → 阶段A
```

### 8.3 完整连接链路（mqtt_full_connect）

```
1. AT 测试          AT\r\n                    → "OK"
2. 设 STA 模式      AT+CWMODE=1\r\n           → "OK"
3. 连接 WiFi        AT+CWJAP="ssid","pwd"    → "WIFI GOT IP"
4. TCP 连接 Broker  AT+CIPMUX=0 → AT+CIPSTART="TCP","host",1883 → "CONNECT"
5. 进入 MQTT 模式   BSP_ESP8266_EnterMQTTMode()  // 切换二进制接收通道
6. MQTT 客户端初始化 MQTTClientInit()
7. MQTT CONNECT    MQTTConnect()             // 发送CONNECT，等待CONNACK
8. 拼接主题         huawei_build_topics()
9. 订阅命令主题     MQTTSubscribe("$oc/.../sys/commands/#")
```

---

## 九、数据流程

### 9.1 上行数据流程（传感器→华为云）

```
DHT11 传感器 (PG9单总线, 5秒周期)
    │
    ▼
DHT11Task
    │ APP_DHT11_Task()
    │ 读取温湿度 → 更新 s_snapshot（临界区保护）
    ▼
┌─────────────────────┐
│  DHT11_Snapshot_t   │  ← 共享快照（生产者写，消费者读）
│  temp_int/temp_dec  │
│  hum_int/hum_dec    │
│  valid              │
└─────────┬───────────┘
          │ APP_DHT11_GetSnapshot()
          ▼
MqttTask（阶段B: mqtt_publish_once）
    │ 读取快照 → snprintf 组装华为云物模型JSON
    │ {"services":[{"service_id":"Sensor",
    │   "properties":{"temperature":28.5,"humidity":52.0}}]}
    ▼
Paho MQTTClient
    │ MQTTPublish() → MQTTSerialize_publish()
    │ 组装 MQTT PUBLISH 报文（固定头+可变头+payload）
    ▼
Network 移植层 (mqtt_port.c)
    │ esp8266_mqttwrite() → BSP_ESP8266_TCPSend()
    │ AT+CIPSEND=len → 等待 ">" → 发送二进制 → 等待 "SEND OK"
    ▼
ESP8266 → WiFi路由器 → Internet → 华为云IoTDA Broker
    │
    ▼
华为云控制台 → 设备 → 属性页面（温度/湿度实时刷新，历史趋势图）
```

### 9.2 下行数据流程（华为云→设备→执行→响应）

```
华为云控制台 → 设备 → 命令 → 下发 LED_Control (led=1)
    │
    ▼
华为云 IoTDA Broker → Internet → WiFi → ESP8266
    │ UART3接收（DMA+IDLE中断）
    ▼
BSP MQTT二进制环形缓冲 (s_mqtt_buf[512], 不追加\n)
    │
    ▼
MqttTask（阶段C: MQTTYield 200ms）
    │ esp8266_mqttread() → BSP_ESP8266_TCPRead()
    │ 从环形缓冲提取原始数据
    ▼
Paho MQTTClient
    │ MQTTDeserialize_publish() 反序列化PUBLISH报文
    │ 匹配订阅主题 → 调用 mqtt_message_handler(MessageData*)
    ▼
mqtt_message_handler()（在MQTTYield上下文中执行）
    │
    ├─ 从主题提取 request_id
    │   $oc/devices/.../sys/commands/request_id=b57bf32b-...
    │
    ├─ 从JSON提取 command_name 和 paras.led
    │   json_get_string(payload, "command_name", ...)
    │   json_get_int(payload, "led", &value)
    │
    ├─ 执行 LED 控制
    │   huawei_exec_led(led_mask)  // bit0~3 → LED1~4
    │
    └─ 构建响应JSON，缓存（不在回调内直接发布）
        {"result_code":0,"response_name":"COMMAND_RESPONSE",
         "paras":{"result":"success","led":1}}
        mqtt_queue_reply(payload, len, request_id)
    │
    ▼
s_reply_payload / s_reply_request_id / s_reply_pending
    │
    ▼
MqttTask（阶段C: MQTTYield返回后）
    │ mqtt_flush_reply()
    │ 拼接响应主题: $oc/devices/.../sys/commands/response/request_id=xxx
    │ MQTTPublish() → ESP8266 → 华为云
    ▼
华为云控制台 → 命令状态变为"已送达"，显示响应 result_code: 0
```

### 9.3 关键设计：回调中不直接发布

下行命令回调 `mqtt_message_handler` 运行在 `MQTTYield` 的读取上下文中。如果在回调内直接调用 `MQTTPublish`，会导致 Paho 接收状态机重入，破坏内部缓冲区。因此：

- 回调中只做：解析命令 → 执行动作 → 将响应数据和 request_id 存入缓存
- 主循环中 `MQTTYield` 返回后调用 `mqtt_flush_reply()` 统一发布响应

---

## 十、业务模式

### 10.1 发布/订阅（Pub/Sub）模式

设备与华为云通过 MQTT Broker 解耦，采用标准发布/订阅模式：

```
                    ┌──────────────────┐
                    │  华为云 IoTDA    │
                    │  MQTT Broker     │
                    └────────┬─────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
        ┌─────▼─────┐  ┌────▼─────┐  ┌────▼─────┐
        │  STM32    │  │ 华为云   │  │ 应用/小程序│
        │  设备     │  │ 控制台   │  │ (可选)    │
        └───────────┘  └──────────┘  └──────────┘
```

- **发布者**：设备发布属性数据；云平台发布命令
- **订阅者**：设备订阅命令主题；云平台订阅属性上报（平台内部自动处理）
- **Broker**：华为云 IoTDA 负责消息路由、设备管理、数据存储

### 10.2 请求/应答模式（命令下发）

下行命令采用请求-应答模式，通过两个主题实现同步语义：

| 方向 | 主题 | 内容 |
|---|---|---|
| 请求（云→设备） | `$oc/devices/{id}/sys/commands/request_id={rid}` | 命令JSON（含command_name、paras） |
| 应答（设备→云） | `$oc/devices/{id}/sys/commands/response/request_id={rid}` | 响应JSON（含result_code） |

request_id 由平台生成，设备必须在响应中原样返回，平台据此匹配请求与应答。

### 10.3 物模型驱动

华为云 IoTDA 采用物模型（Product Model）驱动：
- **属性（Property）**：设备运行状态数据，可上报可查询（temperature、humidity）
- **命令（Command）**：平台对设备的控制指令（LED_Control）
- 设备上报的 JSON 必须符合物模型定义（service_id、属性名、数据类型必须匹配），否则平台丢弃数据

---

## 十一、生产者和消费者

### 11.1 MQTT 层面角色

| 角色 | 实体 | 生产内容 | 消费内容 |
|---|---|---|---|
| **生产者** | STM32 设备 | 传感器属性数据（→ `sys/properties/report`）、命令响应（→ `sys/commands/response`） | — |
| **生产者** | 华为云控制台 | 下行命令（→ `sys/commands/request_id=xxx`） | — |
| **消费者** | STM32 设备 | — | 下行命令（← `sys/commands/#`） |
| **消费者** | 华为云平台 | — | 属性数据（← `sys/properties/report`）、命令响应 |

**STM32 设备既是生产者也是消费者**：
- 作为生产者：周期采集 DHT11 温湿度，以物模型 JSON 上报平台
- 作为消费者：订阅命令主题，接收并执行平台下发的 LED 控制命令

### 11.2 设备内部任务间生产者-消费者

```
DHT11Task（生产者）───DHT11_Snapshot_t───► MqttTask（消费者）
    5秒周期采集温湿度                 3秒周期读取快照并上报

UART3 RX DMA中断（生产者）───环形缓冲───► MqttTask/MQTTYield（消费者）
    硬件中断接收ESP8266数据            轮询读取并解析MQTT报文
```

- **DHT11 快照**：`DHT11_Snapshot_t` 由 DHT11Task 写入，MqttTask 读取，临界区保护
- **UART3 环形缓冲**：DMA+IDLE 中断写入，BSP_ESP8266 读取，MQTT 模式下使用独立 512 字节二进制缓冲

---

## 十二、特点与要点

### 12.1 分层架构与移植性

- **Paho 移植层（mqtt_port）**：将 Paho 依赖的 `Timer` 和 `Network` 两个抽象类型对接到底层硬件。更换网络模块（如 4G 模块）只需重写 `mqtt_port.c`，应用层代码零修改
- **Timer 基于 HAL_GetTick()**：不依赖 FreeRTOS，调度器启动前后均可使用
- **Network 函数指针**：`mqttread`/`mqttwrite`/`disconnect` 三个回调，Paho 核心不关心底层是 ESP8266 还是 W5500

### 12.2 华为云物模型适配

- 属性上报严格遵循 `{"services":[{"service_id":"Sensor","properties":{...}}]}` 格式
- `service_id`、属性名、数据类型必须与控制台物模型定义完全一致
- 命令响应必须携带平台下发的 `request_id`，否则平台无法匹配应答
- 内置轻量 JSON 解析（`json_get_string`/`json_get_int`），不需要额外 JSON 库

### 12.3 二进制安全的 MQTT 接收通道

ESP8266 的 `+IPD` 数据在文本模式下会被追加 `\n`，破坏 MQTT 二进制报文。BSP 层设计双模式：
- **文本模式**（`PollIPD`）：追加 `\n`，适用于文本指令
- **MQTT 模式**（`TCPRead`）：不追加任何字节，二进制安全
- TCP 连接成功后立即调用 `BSP_ESP8266_EnterMQTTMode()` 切换

### 12.4 自动重连机制

连接状态机中任何环节失败（AT测试/WiFi/TCP/MQTT连接/发布/Yield）都：
1. 调用 `MQTTDisconnect()` 清理
2. `osDelay(3000ms)` 等待
3. 从头执行完整连接链路

无需人工干预，网络恢复后自动上线。

### 12.5 Paho 与 HAL 枚举冲突处理

Paho 的 `enum returnCode` 定义了 `SUCCESS/FAILURE/BUFFER_OVERFLOW`，与 STM32 HAL 的 `HAL_StatusTypeDef`（`SUCCESS=0U`）冲突。`mqtt_port.h` 通过宏重命名解决：

```c
#define SUCCESS          MQTT_SUCCESS
#define FAILURE          MQTT_FAILURE
#define BUFFER_OVERFLOW  MQTT_BUFFER_OVERFLOW
#include "MQTTClient.h"
#undef SUCCESS
#undef FAILURE
#undef BUFFER_OVERFLOW
```

仅在包含 Paho 头文件期间生效，不污染全局命名空间。

### 12.6 静态分配避免栈溢出

MQTT 客户端实例、Network、发送/接收缓冲区（各 512 字节）均为静态全局变量，任务栈仅需 4KB。

### 12.7 回调防重入设计

下行命令回调中不直接调用 `MQTTPublish`，而是将响应数据和 request_id 缓存，在主循环 `MQTTYield` 返回后统一发布，避免 Paho 接收状态机重入。

### 12.8 编译警告修复

Paho 原版代码中 `enum QoS` 因包含 `SUBFAIL=0x80` 被 GCC 压缩为 1 字节，但直接强转为 `int*`（4字节）传给期望 `int[]` 的函数，触发 `-Wstringop-overflow` 和 `-Waddress-of-packed-member` 警告。修复方式：使用临时 `int` 变量中转。

### 12.9 非加密接入的选择

华为云 IoTDA 同时支持 MQTT（1883）和 MQTTS（8883/TLS）。本实验使用非加密 MQTT，原因：
- ESP8266 AT 指令 + Paho 移植层未实现 TLS 证书校验
- 非加密接入域名与 MQTTS 相同，仅端口不同（1883 vs 8883）
- 实验和原型阶段非加密足够，生产环境建议升级 TLS

---

## 十三、实验结果与验证

### 13.1 连接验证

串口日志确认全链路连通：
```
[MQTT] AT OK → WiFi connected → TCP connected → MQTT connected → Subscribed → Published
```

### 13.2 属性上报验证

- 上报周期：3 秒/次
- 上报数据：`{"services":[{"service_id":"Sensor","properties":{"temperature":28.5,"humidity":52.0}}]}`
- 控制台属性页面：temperature、humidity 实时刷新，历史数据自动存储

### 13.3 命令下发验证

| 命令参数 | 设备行为 | 控制台响应 |
|---|---|---|
| `led=1` | LED1 点亮 | result_code: 0 |
| `led=0` | 全部熄灭 | result_code: 0 |
| `led=2` | LED2 点亮 | result_code: 0 |
| `led=15` | 全部点亮 | result_code: 0 |

串口日志：
```
[MQTT] RECV topic=$oc/devices/your_device_id/sys/commands/request_id=b57bf32b-...
[MQTT] payload: {"paras":{"led":1},"service_id":"Sensor","command_name":"LED_Control"}
[MQTT-CMD] Huawei command: LED_Control, led=1
[MQTT] Command response sent (request_id=b57bf32b-...)
```

### 13.4 设备状态验证

- 初始状态：未激活（灰色）
- 首次 MQTT 连接成功后：自动变为**在线**（绿色圆点）
- 断线后：变为离线，自动重连成功后恢复在线

### 13.5 鲁棒性验证

- 断开 WiFi 路由器：设备检测到 Yield 失败 → 3 秒后重连 → WiFi 恢复后自动上线
- 长时间运行（30分钟+）：属性上报稳定，偶发 ESP8266 AT 半双工碰撞导致的断开可自动恢复

---

## 十四、常见问题与排查

| 问题 | 原因 | 解决方法 |
|---|---|---|
| `WiFi join FAILED` | WiFi 名称/密码错，或不是 2.4GHz | 检查 `MQTT_WIFI_SSID`/`MQTT_WIFI_PASSWORD`，ESP8266 不支持 5GHz |
| `TCP connect FAILED` | 域名 DNS 解析失败 | 确认域名为 `iot-mqtts`（不要改成 `iot-mqtt`）；可 ping 域名换 IP |
| `MQTT CONNECT FAILED rc=-1` | 鉴权三要素错 | 重新用在线工具生成，确认选"不校验时间戳"；Password 是 HMAC-SHA256 签名不是 device_secret |
| `MQTT CONNECT FAILED rc=-2` | ClientId 重复 | 确保只有一个设备用此 ClientId 连接 |
| 属性上报后平台看不到数据 | service_id 或属性名不匹配 | 检查代码中 `HUAWEI_SERVICE_ID` 和属性名是否与物模型完全一致 |
| 命令下发设备没反应 | 订阅主题错误 | 确认订阅 `$oc/devices/{id}/sys/commands/#` |
| 命令状态一直"等待响应" | 没发响应或 request_id 不对 | 确认响应主题包含正确的 request_id |
| 偶发 Publish/Yield 失败后重连 | ESP8266 AT 半双工碰撞 | 正常现象，自动重连即可；可调小 Yield 超时或增大发布周期 |

---

## 十五、实验总结

本实验成功实现了 STM32F407 通过 ESP8266 接入华为云 IoTDA 的完整物联网应用，涵盖：

1. **云平台配置**：产品创建、物模型定义（属性+命令）、设备注册、鉴权参数生成
2. **网络接入**：AT 指令驱动 ESP8266 连接 WiFi 并建立 TCP 连接
3. **MQTT 通信**：基于 Paho 库实现 CONNECT/SUBSCRIBE/PUBLISH/YIELD 完整流程
4. **数据上报**：DHT11 温湿度以华为云物模型 JSON 格式周期上报，平台存储并生成趋势
5. **远程控制**：控制台下发 LED_Control 命令，设备解析执行并回复响应
6. **系统稳定性**：自动重连、静态分配、回调防重入、二进制安全接收

项目采用分层架构和条件编译，具有良好的可移植性和可裁剪性，为后续扩展更多传感器、接入其他云平台或更换网络模块奠定了基础。
