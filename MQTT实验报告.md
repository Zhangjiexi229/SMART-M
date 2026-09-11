# STM32F407 + ESP8266 + Paho MQTT 上云实验报告

---

## 一、实验概述

本实验基于 STM32F407 开发板，通过 ESP8266 WiFi 模块以 AT 指令方式接入网络，使用 Eclipse Paho MQTT Embedded C 客户端库连接公共 MQTT Broker（broker.emqx.io），实现：

- **传感器数据周期上报**：DHT11 温湿度数据以 JSON 格式发布到云平台
- **云平台下行指令控制**：接收文本指令或十六进制帧，控制板载 LED，并回复执行结果
- **断线自动重连**：WiFi/TCP/MQTT 任一环节失败后延迟 3 秒从头重连

---

## 二、硬件与软件环境

| 项目 | 内容 |
|---|---|
| MCU | STM32F407（Cortex-M4，168MHz） |
| WiFi 模块 | ESP8266（UART3，PB10=TX，PB11=RX，115200 8N1） |
| 传感器 | DHT11 温湿度（PG9 单总线） |
| 调试串口 | USART1（PA9/PA10，115200） |
| 操作系统 | FreeRTOS（CMSIS-RTOS2 封装） |
| MQTT 库 | Eclipse Paho MQTT Embedded C（同步客户端） |
| MQTT 版本 | MQTT 3.1.1 |
| 构建系统 | CMake + STM32CubeMX |
| Broker | broker.emqx.io:1883（公共测试 Broker） |

---

## 三、MQTT 配置

所有配置集中在 `usrcode/APP/Inc/app_mqtt.h` 的用户配置区，修改后重新编译即可生效。

### 3.1 WiFi 配置

```c
#define MQTT_WIFI_SSID        "spring"        // 2.4GHz WiFi 名称
#define MQTT_WIFI_PASSWORD    "mo12345678"    // WiFi 密码
```

> ESP8266 仅支持 2.4GHz，不支持 5GHz。

### 3.2 Broker 与客户端配置

```c
#define MQTT_BROKER_HOST      "broker.emqx.io"   // Broker 地址（域名或IP）
#define MQTT_BROKER_PORT      1883U              // MQTT 端口（SSL为8883）
#define MQTT_CLIENT_ID        "stm32f407_kdl_01" // 客户端ID（必须全局唯一）
#define MQTT_USERNAME         ""                 // 用户名（无认证留空）
#define MQTT_PASSWORD         ""                 // 密码（无认证留空）
#define MQTT_KEEPALIVE_SEC    60U                // 心跳间隔（秒）
```

### 3.3 主题（Topic）配置

| 宏 | 值 | 方向 | 说明 |
|---|---|---|---|
| `MQTT_PUB_TOPIC` | `sensor/dht11` | 设备→云 | 传感器数据上报 |
| `MQTT_SUB_TOPIC` | `device/kdl/cmd` | 云→设备 | 下行指令接收 |
| `MQTT_REPLY_TOPIC` | `device/kdl/reply` | 设备→云 | 指令执行结果回复 |
| `MQTT_QOS` | `0` | — | QoS 等级（0/1/2） |

### 3.4 运行参数

```c
#define MQTT_PUBLISH_PERIOD_MS   3000U   // 数据上报周期（毫秒）
#define MQTT_RECONNECT_DELAY_MS  3000U   // 断线重连等待（毫秒）
#define MQTT_YIELD_TIMEOUT_MS    200     // 每次 MQTTYield 超时（毫秒）
#define MQTT_TASK_STACK_SIZE     (1024*4) // 任务栈 4KB
#define MQTT_TASK_PRIORITY       osPriorityNormal
```

### 3.5 模块开关

在 `usrcode/Common/Inc/module_cfg.h` 中：

```c
#define BSP_ESP8266_ENABLE  1   // ESP8266 驱动（依赖 BSP_UART3_ENABLE）
#define APP_MQTT_ENABLE     1   // MQTT 上云任务
#define APP_DHT11_ENABLE    1   // DHT11 采集任务（数据源）
#define PROTOCOL_ENABLE     1   // 下行指令协议解析
```

> **互斥约束**：`APP_MQTT_ENABLE` 与 `APP_WIFI_ENABLE` 不能同时为 1（共用 ESP8266）；`BSP_ESP8266_ENABLE` 与 `APP_UART3_CMD_ENABLE` 不能同时为 1（共用 UART3）。编译时以 `#error` 强制检查。

---

## 四、具体操作流程

### 4.1 编译与烧录

1. 用 VS Code 打开项目目录，安装 CMake 插件
2. 修改 `app_mqtt.h` 中的 WiFi SSID/密码为实际环境
3. 选择 `Debug` 配置，点击 Build
4. 生成 `pro_v2.1.hex` / `pro_v2.1.bin`，用 ST-Link 烧录

### 4.2 运行验证

1. 打开串口调试助手（115200, 8N1），连接 USART1
2. 复位开发板，观察串口输出：
   ```
   [MQTT] Task started, broker=broker.emqx.io:1883, publish every 3000ms
   [MQTT] ===== Connecting to cloud =====
   [MQTT] AT test...
   [MQTT] AT OK
   [MQTT] Joining AP "spring" ...
   [MQTT] WiFi connected
   [MQTT] TCP connecting broker.emqx.io:1883 ...
   [MQTT] TCP connected
   [MQTT] MQTT connected (client=stm32f407_kdl_01)
   [MQTT] Subscribed to "device/kdl/cmd" (QoS=0)
   [MQTT] ===== All connected, start publishing =====
   ```
3. 每 3 秒输出一次传感器数据发布（可在 MQTTX 客户端订阅 `sensor/dht11` 查看）

### 4.3 MQTTX 客户端测试

**查看传感器数据（订阅）：**
- 连接 broker.emqx.io:1883
- 订阅主题 `sensor/dht11`
- 收到 JSON 数据：
  ```json
  {"device":"stm32f407_kdl_01","temperature":25.5,"humidity":60.0,"unit":{"temperature":"C","humidity":"%"}}
  ```

**发送下行指令（发布）：**
- 向主题 `device/kdl/cmd` 发布文本指令：
  - `LED1ON` / `LED1OFF` — 控制 LED1
  - `LED2ON` / `LED2OFF` — 控制 LED2
  - `ALLON` / `ALLOFF` — 全部控制
  - `HELP` — 查询帮助
- 订阅主题 `device/kdl/reply` 接收回复：`LED1 ON\r\n`

**发送十六进制帧（发布）：**
- 8 字节固定帧，B0 高 4 位 = 0x1 标识十六进制帧
- 例：`10 10 00 10 00 01 00 XX` — 写 LED 状态寄存器，XX 为 CRC8

---

## 五、增加和修改的文件

### 5.1 新增文件

| 文件路径 | 说明 |
|---|---|
| `usrcode/APP/Inc/app_mqtt.h` | MQTT 任务头文件：配置宏、API 声明 |
| `usrcode/APP/Src/app_mqtt.c` | MQTT 任务实现：连接状态机、数据发布、下行处理 |
| `usrcode/Protocol/Inc/mqtt_port.h` | Paho 移植层头文件：Timer + Network 类型定义 |
| `usrcode/Protocol/Src/mqtt_port.c` | Paho 移植层实现：Timer(基于HAL tick) + Network(对接ESP8266) |
| `Middlewares/Third_Party/paho/MQTTClient-C/src/MQTTClient.c` | Paho 同步客户端（第三方库） |
| `Middlewares/Third_Party/paho/MQTTClient-C/src/MQTTClient.h` | Paho 客户端头文件 |
| `Middlewares/Third_Party/paho/MQTTPacket/src/*.c/h` | Paho 报文序列化/反序列化库（6个源文件） |

### 5.2 修改文件

| 文件路径 | 修改内容 |
|---|---|
| `usrcode/Common/Inc/module_cfg.h` | 新增 `APP_MQTT_ENABLE` 开关及与 `APP_WIFI_ENABLE` 的互斥 `#error` 检查 |
| `usrcode/APP/Src/app_tasks.c` | 新增 MQTT 任务句柄、属性、创建代码（`osThreadNew(APP_MQTT_Task, ...)`） |
| `CMakeLists.txt` | 新增 Paho MQTT 源文件列表、include 路径、`MQTTCLIENT_PLATFORM_HEADER` 编译宏 |
| `Middlewares/Third_Party/paho/MQTTClient-C/src/MQTTClient.c` | 修复 `enum QoS`（1字节）强转 `int*`（4字节）导致的 `-Wstringop-overflow` 和 `-Waddress-of-packed-member` 警告，改用临时 `int` 变量中转 |

### 5.3 Paho 库编译裁剪

CMakeLists.txt 中仅编译客户端需要的 7 个文件，排除服务端文件（`*Server.c`）和工具文件（`MQTTFormat.c`），减小固件体积：

```
MQTTPacket.c          — 通用工具
MQTTConnectClient.c   — CONNECT 报文
MQTTDeserializePublish.c — PUBLISH 反序列化
MQTTSerializePublish.c   — PUBLISH 序列化
MQTTSubscribeClient.c    — SUBSCRIBE/SUBACK
MQTTUnsubscribeClient.c  — UNSUBSCRIBE/UNSUBACK
MQTTClient.c             — 同步客户端主逻辑
```

---

## 六、项目框架

### 6.1 整体分层架构

```
┌─────────────────────────────────────────────────────────┐
│                    APP 应用层                            │
│  app_mqtt.c  app_dht11.c  app_key.c  app_cmd.c  ...     │
│  （业务逻辑、任务、状态机）                                │
├─────────────────────────────────────────────────────────┤
│                  Protocol 协议层                         │
│  mqtt_port.c  protocol.c  protocol_hex.c                │
│  （Paho移植、文本协议、十六进制帧协议）                    │
├─────────────────────────────────────────────────────────┤
│                    BSP 板级支持包                        │
│  bsp_esp8266.c  bsp_uart.c  bsp_dht11.c  bsp_led.c ...  │
│  （硬件驱动、AT指令、DMA接收）                            │
├─────────────────────────────────────────────────────────┤
│                    Common 公共层                         │
│  module_cfg.h  ring_buffer.c  common.c                  │
│  （模块开关、环形缓冲、通用工具）                          │
├─────────────────────────────────────────────────────────┤
│              Core / HAL / FreeRTOS                      │
│  STM32CubeMX 生成：GPIO/UART/ADC/DMA/TIM + FreeRTOS     │
└─────────────────────────────────────────────────────────┘
```

### 6.2 目录结构

```
pro_v2.1/
├── Core/                    # STM32CubeMX 生成（HAL初始化、main.c、freertos.c）
├── Drivers/                 # STM32 HAL 驱动库
├── Middlewares/
│   └── Third_Party/
│       └── paho/            # Paho MQTT Embedded C 第三方库
│           ├── MQTTClient-C/src/   # 同步客户端
│           └── MQTTPacket/src/     # 报文编解码
├── usrcode/                 # 用户代码（分层组织）
│   ├── APP/                 # 应用层
│   │   ├── Inc/             #   app_mqtt.h, app_dht11.h, app_tasks.h ...
│   │   └── Src/             #   app_mqtt.c, app_dht11.c, app_tasks.c ...
│   ├── BSP/                 # 板级支持包
│   │   ├── Inc/             #   bsp_esp8266.h, bsp_uart.h, bsp_dht11.h ...
│   │   └── Src/             #   bsp_esp8266.c, bsp_uart.c, bsp_dht11.c ...
│   ├── Protocol/            # 协议层
│   │   ├── Inc/             #   mqtt_port.h, protocol.h, protocol_hex.h
│   │   └── Src/             #   mqtt_port.c, protocol.c, protocol_hex.c
│   └── Common/              # 公共层
│       ├── Inc/             #   module_cfg.h, ring_buffer.h, common.h
│       └── Src/             #   ring_buffer.c, common.c
├── CMakeLists.txt           # 构建配置
└── pro.ioc                  # STM32CubeMX 工程配置
```

### 6.3 条件编译模块化设计

所有模块通过 `module_cfg.h` 中的宏开关控制，禁用模块不生成任何代码：

- **BSP 层**：`BSP_ESP8266_ENABLE`、`BSP_DHT11_ENABLE`、`BSP_UART1_ENABLE` 等
- **Protocol 层**：`PROTOCOL_ENABLE`
- **APP 层**：`APP_MQTT_ENABLE`、`APP_DHT11_ENABLE`、`APP_KEY_ENABLE` 等
- **互斥保护**：共用硬件的模块以 `#error` 编译期检查

---

## 七、程序流程

### 7.1 系统启动流程

```
main()
  │
  ├─ HAL_Init()                    // HAL库初始化
  ├─ SystemClock_Config()          // 168MHz 系统时钟
  ├─ MX_GPIO_Init()                // GPIO初始化
  ├─ MX_DMA_Init()                 // DMA初始化
  ├─ MX_USART1_UART_Init()         // 调试串口
  ├─ MX_USART3_UART_Init()         // ESP8266通信串口
  ├─ MX_ADC3_Init() / MX_TIM2_Init()
  │
  ├─ osKernelInitialize()          // FreeRTOS内核初始化
  ├─ MX_FREERTOS_Init()
  │     ├─ APP_Init()              // BSP硬件初始化
  │     │    ├─ BSP_Delay_Init()   // TIM2自由计数器（微秒延时）
  │     │    ├─ BSP_UART1_Init()   // UART1 DMA+IDLE接收
  │     │    ├─ BSP_UART3_Init()   // UART3 DMA+IDLE接收
  │     │    ├─ BSP_ESP8266_Init() // ESP8266驱动初始化
  │     │    ├─ BSP_DHT11_Init()   // DHT11初始化
  │     │    └─ LED自检闪烁3次
  │     ├─ APP_TASKS_CreateObjects()  // 创建流缓冲等RTOS对象
  │     └─ APP_TASKS_CreateTasks()    // 创建所有应用任务
  │           ├─ DHT11Task         // 温湿度采集（5秒周期）
  │           ├─ KeyScanTask       // 按键扫描
  │           ├─ Uart1RelayTask    // UART1数据转发
  │           ├─ Cmd1Task          // UART1指令解析
  │           └─ MqttTask          // MQTT上云任务 ★
  │
  └─ osKernelStart()               // 启动调度器（不再返回）
```

### 7.2 MQTT 任务状态机

```
        ┌──────────────────────────────────────────┐
        │              MqttTask                    │
        │  (osDelay 500ms 等待其他任务初始化)       │
        └──────────────┬───────────────────────────┘
                       │
                       ▼
        ┌──────────────────────────┐
        │  阶段A：未连接             │
        │  s_mqtt_status = 0        │
        │  mqtt_full_connect()      │◄──────────────┐
        └──────┬───────────────────┘               │
               │ 成功                               │ 失败
               ▼                                    │
        ┌──────────────────────────┐               │
        │  阶段B：发布数据           │               │
        │  mqtt_publish_once()     │               │
        │  (组装JSON → MQTTPublish) │               │
        └──────┬───────────────────┘               │
               │ 成功                               │ 失败
               ▼                                    │
        ┌──────────────────────────┐               │
        │  阶段C：维持连接           │               │
        │  while(waited < 3000ms)  │               │
        │    MQTTYield(200ms)      │               │
        │    mqtt_flush_reply()    │               │
        │    waited += 200         │               │
        └──────┬───────────────────┘               │
               │ 周期到 → 回到阶段B                  │ Yield失败
               │                                    │
               └────────────────────────────────────┘
                          失败 → MQTTDisconnect → osDelay(3000ms) → 阶段A
```

### 7.3 完整连接链路（mqtt_full_connect）

```
1. AT 测试          → AT\r\n                → 期待 "OK"
2. 设 STA 模式      → AT+CWMODE=1\r\n       → 期待 "OK"
3. 连接 WiFi        → AT+CWJAP="ssid","pwd" → 期待 "WIFI GOT IP"
4. TCP 连接 Broker  → AT+CIPMUX=0 → AT+CIPSTART="TCP","host",1883 → "CONNECT"
5. 进入 MQTT 模式   → BSP_ESP8266_EnterMQTTMode()（切换二进制接收通道）
6. MQTT 客户端初始化 → MQTTClientInit()（绑定 Network、缓冲区、超时5s）
7. MQTT CONNECT     → MQTTConnect()（发送 CONNECT 报文，等待 CONNACK）
8. 订阅主题         → MQTTSubscribe("device/kdl/cmd", QoS0, handler)
```

---

## 八、数据流程

### 8.1 上行数据流程（传感器→云平台）

```
DHT11 传感器
    │ (PG9 单总线, 5秒周期采集)
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
MqttTask（阶段B）
    │ mqtt_publish_once()
    │ 读取快照 → snprintf 组装 JSON
    │ {"device":"...","temperature":25.5,"humidity":60.0,"unit":{...}}
    ▼
Paho MQTTClient
    │ MQTTPublish() → MQTTSerialize_publish()
    │ 组装 MQTT PUBLISH 报文（固定头+可变头+payload）
    ▼
Network 移植层
    │ esp8266_mqttwrite() → BSP_ESP8266_TCPSend()
    │ AT+CIPSEND=len → 等待 ">" → 发送二进制数据 → 等待 "SEND OK"
    ▼
ESP8266 → WiFi 路由器 → Internet → MQTT Broker
    │
    ▼
 订阅者（MQTTX / 云平台）收到 sensor/dht11 主题的 JSON 数据
```

### 8.2 下行数据流程（云平台→设备→执行→回复）

```
云平台 / MQTTX 客户端
    │ 向 device/kdl/cmd 发布消息（文本或十六进制帧）
    ▼
MQTT Broker → WiFi → ESP8266
    │ UART3 接收（DMA+IDLE中断）
    ▼
BSP 环形缓冲（s_mqtt_buf[512]，MQTT二进制模式，不追加\n）
    │
    ▼
MqttTask（阶段C: MQTTYield）
    │ esp8266_mqttread() → BSP_ESP8266_TCPRead()
    │ 从环形缓冲提取原始数据
    ▼
Paho MQTTClient
    │ MQTTDeserialize_publish() 反序列化 PUBLISH 报文
    │ 匹配订阅主题 → 调用 mqtt_message_handler(MessageData*)
    ▼
mqtt_message_handler()（在 MQTTYield 上下文中执行）
    │
    ├─ 逐字节喂入十六进制解析器（优先）
    │    ├─ 完整帧 + CRC正确 → APP_HexCmd_Process() → 应答帧缓存
    │    └─ CRC错误 → 打印日志，丢弃
    │
    ├─ 非十六进制字节 → 喂入文本协议解析器
    │    └─ 完整行(\n结尾) → Protocol_Feed 匹配指令
    │         ├─ LED1ON/LED2ON/... → APP_CMD_Exec() 控制LED
    │         └─ 回复字符串缓存
    │
    └─ 消息末尾强制成帧（补\n触发残留文本匹配）
    │
    ▼
s_reply_payload / s_reply_pending（回复缓存，不在回调内直接发布）
    │
    ▼
MqttTask（阶段C: MQTTYield 返回后）
    │ mqtt_flush_reply()
    │ MQTTPublish("device/kdl/reply", 回复数据)
    ▼
ESP8266 → WiFi → Broker → 订阅者收到回复
```

### 8.3 关键设计：回调中不直接发布

下行消息回调 `mqtt_message_handler` 运行在 `MQTTYield` 的读取上下文中。如果在回调内直接调用 `MQTTPublish`，会导致 Paho 接收状态机重入，破坏内部缓冲区。因此：

- 回调中只做：解析指令 → 执行动作 → 将回复数据存入 `s_reply_payload`
- 主循环中 `MQTTYield` 返回后调用 `mqtt_flush_reply()` 统一发布

---

## 九、业务模式

### 9.1 发布/订阅（Pub/Sub）模式

本项目采用 MQTT 标准的发布/订阅模式，设备与云平台通过 Broker 解耦：

```
                    ┌──────────────┐
                    │  MQTT Broker │
                    │ (emqx.io)    │
                    └──────┬───────┘
                           │
          ┌────────────────┼────────────────┐
          │                │                │
    ┌─────▼─────┐   ┌──────▼──────┐  ┌─────▼─────┐
    │  STM32    │   │  MQTTX 客户端│  │  云平台   │
    │  设备     │   │  (调试用)    │  │  (可选)   │
    └───────────┘   └─────────────┘  └───────────┘
```

- **发布者**：向主题发送消息，不需要知道订阅者是谁
- **订阅者**：订阅感兴趣的主题，不需要知道发布者是谁
- **Broker**：负责消息路由，支持主题通配符（`+` 单层、`#` 多层）

### 9.2 请求/应答模式（下行指令）

下行指令采用请求-应答模式，通过两个主题实现：

| 方向 | 主题 | 内容 |
|---|---|---|
| 请求（云→设备） | `device/kdl/cmd` | 文本指令或十六进制帧 |
| 应答（设备→云） | `device/kdl/reply` | 执行结果文本或应答帧 |

云平台发布指令后，订阅 `device/kdl/reply` 等待设备回复，实现同步语义。

### 9.3 双协议并存

下行通道同时支持两种协议，通过首字节自动区分：

| 协议 | 标识 | 帧格式 | 适用场景 |
|---|---|---|---|
| 文本行协议 | 首字节 ≠ 0x10~0x1F | `LED1ON\n`（以\n结尾） | 人工调试、MQTTX手动发送 |
| 十六进制帧 | B0高4位 = 0x1 | 固定8字节 + CRC8 | 程序自动化、类Modbus寄存器模型 |

十六进制帧格式（大端序）：
```
B0: [7:4]版本=0x1  [3:0]设备地址
B1: 功能码 CMD
B2-B3: 寄存器地址 REG
B4-B5: 数据 DATA
B6: [7]应答标志  [6:0]帧序号
B7: CRC8(poly=0x07, init=0x00)
```

---

## 十、生产者和消费者

### 10.1 角色定义

在 MQTT 发布/订阅模型中，本系统的生产者和消费者角色如下：

| 角色 | 实体 | 生产内容 | 消费内容 |
|---|---|---|---|
| **生产者** | STM32 设备 | 传感器数据（→ `sensor/dht11`）、指令回复（→ `device/kdl/reply`） | — |
| **生产者** | 云平台/MQTTX | 下行指令（→ `device/kdl/cmd`） | — |
| **消费者** | STM32 设备 | — | 下行指令（← `device/kdl/cmd`） |
| **消费者** | 云平台/MQTTX | — | 传感器数据（← `sensor/dht11`）、指令回复（← `device/kdl/reply`） |

**STM32 设备既是生产者也是消费者**：
- 作为生产者：周期采集 DHT11 数据，发布到云平台
- 作为消费者：订阅指令主题，接收并执行云平台下发的控制命令

### 10.2 内部生产者-消费者（任务间）

在设备内部，FreeRTOS 任务之间也存在生产者-消费者关系：

```
DHT11Task（生产者）───快照───► MqttTask（消费者）
    采集温湿度                 读取快照并上报

UART3 RX DMA（生产者）──环形缓冲───► MqttTask/MQTTYield（消费者）
    硬件中断接收                 轮询读取并解析
```

- **DHT11 快照**：`DHT11_Snapshot_t` 由 DHT11Task 写入，MqttTask 读取，临界区保护
- **UART3 环形缓冲**：DMA+IDLE 中断写入，BSP_ESP8266 读取，MQTT 模式下使用独立的 512 字节二进制缓冲

---

## 十一、特点与要点

### 11.1 分层架构与移植性

- **Paho 移植层（mqtt_port）**：将 Paho 依赖的 `Timer` 和 `Network` 两个抽象类型对接到底层硬件。更换网络模块（如 4G 模块）只需重写 `mqtt_port.c`，应用层代码零修改
- **Timer 基于 HAL_GetTick()**：不依赖 FreeRTOS，调度器启动前后均可使用
- **Network 函数指针**：`mqttread`/`mqttwrite`/`disconnect` 三个回调，Paho 核心不关心底层是 ESP8266 还是 W5500

### 11.2 条件编译模块化裁剪

- 每个模块顶部 `#include "module_cfg.h"` + `#if XXX_ENABLE` 包裹全部实现
- 禁用模块不生成任何代码，节省 Flash/RAM
- 共用硬件的模块以 `#error` 做编译期互斥检查，避免运行时冲突

### 11.3 二进制安全的 MQTT 接收通道

ESP8266 的 `+IPD` 数据在文本模式下会被追加 `\n`，破坏 MQTT 二进制报文。为此 BSP 层设计了双模式：

- **文本模式**（`BSP_ESP8266_PollIPD`）：追加 `\n`，适用于文本指令
- **MQTT 模式**（`BSP_ESP8266_TCPRead`）：不追加任何字节，二进制安全
- TCP 连接成功后立即调用 `BSP_ESP8266_EnterMQTTMode()` 切换

### 11.4 自动重连机制

连接状态机中任何环节失败（AT测试/WiFi/TCP/MQTT连接/发布/Yield）都：
1. 调用 `MQTTDisconnect()` 清理
2. `osDelay(3000ms)` 等待
3. 从头执行完整连接链路

无需人工干预，网络恢复后自动上线。

### 11.5 Paho 与 HAL 枚举冲突处理

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

### 11.6 静态分配避免栈溢出

MQTT 客户端实例、Network、发送/接收缓冲区（各 512 字节）均为静态全局变量：

```c
static MQTTClient    s_mqtt_client;
static Network       s_mqtt_network;
static unsigned char s_mqtt_sendbuf[512];
static unsigned char s_mqtt_readbuf[512];
```

任务栈仅需 4KB，避免在栈上分配大缓冲区导致溢出。

### 11.7 JSON 数据格式带单位标识

上报数据采用自描述 JSON，`unit` 对象标注各字段单位，便于云平台解析：

```json
{
  "device": "stm32f407_kdl_01",
  "temperature": 25.5,
  "humidity": 60.0,
  "unit": {
    "temperature": "C",
    "humidity": "%"
  }
}
```

传感器未就绪时对应字段不出现（`unit` 中也不出现），避免发送无效数据。

### 11.8 编译警告修复

Paho 原版代码中 `enum QoS` 因包含 `SUBFAIL=0x80` 被 GCC 压缩为 1 字节，但直接强转为 `int*`（4字节）传给期望 `int[]` 的函数，触发 `-Wstringop-overflow` 和 `-Waddress-of-packed-member` 警告。修复方式：使用临时 `int` 变量中转，功能等价且消除警告。

---

## 十二、实验总结

本实验成功实现了 STM32F407 通过 ESP8266 接入 MQTT 云平台的完整功能，涵盖：

1. **网络接入**：AT 指令驱动 ESP8266 连接 WiFi 并建立 TCP 连接
2. **MQTT 通信**：基于 Paho 库实现 CONNECT/SUBSCRIBE/PUBLISH/YIELD 完整流程
3. **数据上报**：DHT11 温湿度以 JSON 格式周期上报
4. **远程控制**：支持文本指令和十六进制帧两种下行协议，控制 LED 并回复
5. **系统稳定性**：自动重连、静态分配、回调防重入、二进制安全接收

项目采用分层架构和条件编译，具有良好的可移植性和可裁剪性，为后续扩展更多传感器、接入私有云平台或更换网络模块奠定了基础。
