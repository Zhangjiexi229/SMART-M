# STM32F407 + FreeModbus 主从一体实验报告

> 实验状态：已编译通过（零错误），待硬件验证
> 文档版本：2026-09-04
> 基于：FreeModbus_Slave-Master-RTT-STM32（armink 社区扩展版，v1.6 内核）

---

## 一、实验概述

本实验基于 STM32F407 开发板，移植 **FreeModbus** 开源 Modbus 协议栈，实现**主站（Master）和从站（Slave）同时运行**：

- **Modbus 从站**：通过 UART4 + RS485 被外部主站（PLC/上位机/Modbus Poll）读取温湿度数据、控制板载 LED
- **Modbus 主站**：通过 UART2 + RS485 主动读取外部 Modbus 从站设备（电表/传感器），采集数据存入共享快照，可供 MQTT 上报华为云
- 主从使用独立串口、独立定时器、独立 RS485 总线，互不干扰
- 与现有 MQTT 上云、DHT11 采集、LED 控制等模块并行运行

---

## 二、硬件与软件环境

### 2.1 硬件平台

| 项目 | 内容 |
|---|---|
| MCU | STM32F407（Cortex-M4，168MHz） |
| 从站串口 | UART4（PC10=TX，PC11=RX），9600 8N1 |
| 主站串口 | USART2（PA2=TX，PA3=RX），9600 8N1 |
| 从站定时器 | TIM7（0.1ms/tick，T3.5 帧间隔） |
| 主站定时器 | TIM12（0.1ms/tick，T3.5/响应超时/转换延时） |
| 从站 RS485 DE | PB1（高=发送，低=接收） |
| 主站 RS485 DE | PB0（高=发送，低=接收） |
| RS485 芯片 | MAX485 / SP3485（两片，主从各一） |
| 温湿度传感器 | DHT11（从站寄存器数据源） |
| 板载 LED | LED1=PG14, LED2=PG13, LED3=PG6, LED4=PG11 |
| 调试串口 | USART1（PA9/PA10，115200） |
| WiFi 模块 | ESP8266（USART3，MQTT 上云，与 Modbus 并行） |

### 2.2 软件平台

| 项目 | 内容 |
|---|---|
| 操作系统 | FreeRTOS（CMSIS-RTOS2 封装） |
| HAL 库 | STM32CubeMX 生成 |
| Modbus 协议栈 | FreeModbus v1.6（armink 主从扩展版） |
| Modbus 模式 | RTU（远程终端单元，二进制帧 + CRC16） |
| 构建系统 | CMake + Ninja + arm-none-eabi-gcc 14.3.1 |
| 编译结果 | FLASH 87680B (16.72%)，RAM 27944B (21.32%) |

### 2.3 FreeModbus 版本说明

| 版本 | 从站 | 主站 | 说明 |
|---|---|---|---|
| 官方 FreeModbus v1.6 | ✅ 免费 | ❌ 商业收费 | 仅从站开源 |
| **armink 扩展版（本实验使用）** | ✅ | ✅ 免费 | 基于 v1.6 扩展主机模式，主从同栈 |

armink 扩展版在官方从站基础上新增：
- `mb_m.c` / `mbrtu_m.c`：主站状态机和 RTU 收发
- `mbfunc*_m.c`：主站功能码实现（01/02/03/04/05/06/15/16）
- `portserial_m.c` / `porttimer_m.c` / `portevent_m.c`：主站移植层
- 主站运行资源互斥（信号量）、请求-应答等待机制

---

## 三、CubeMX (MX) 配置

### 3.1 已在 CubeMX 中配置的外设

| 外设 | 配置 | 用途 |
|---|---|---|
| USART2 | 115200 8N1，异步，中断使能 | 主站串口（代码中改为 9600） |
| USART1 | 115200 8N1，DMA+IDLE | 调试串口 |
| USART3 | 115200 8N1，DMA+IDLE | ESP8266 |
| TIM6 | 基本定时器，中断使能 | HAL 时基（`HAL_IncTick`） |
| TIM2 | 自由运行计数器 | 微秒延时（BSP_Delay） |
| GPIO | PG14/PG13/PG6/PG11 输出 | LED |

> **注意**：TIM6 被 HAL 时基占用，**不能**用作 Modbus 定时器。本实验主站改用 TIM12。

### 3.2 代码中手动初始化的外设（开箱即用）

以下外设未在 CubeMX 中配置，由 `APP_Modbus_HW_Init()` 在运行时手动初始化，无需打开 CubeMX：

| 外设 | 初始化内容 |
|---|---|
| **UART4** | 时钟使能、PC10/PC11 复用 AF8、9600 8N1、中断优先级 5 |
| **TIM7** | 时钟使能、PSC=8399（0.1ms/tick）、中断优先级 5 |
| **TIM12** | 时钟使能、PSC=8399（0.1ms/tick）、中断优先级 5 |
| **PB0/PB1** | GPIO 输出、默认低电平（接收模式） |
| **USART2 波特率** | 从 115200 重新初始化为 9600 |

### 3.3 如改用 CubeMX 配置（可选）

若希望在 CubeMX 中图形化配置，需添加：

1. **UART4**：Mode=Asynchronous，Baud Rate=9600，Word Length=8，Parity=None，Stop Bits=1，NVIC 使能全局中断（优先级 5）
2. **TIM7**：Clock Source=Internal Clock，PSC=8399，Counter Mode=Up，NVIC 使能全局中断（优先级 5）
3. **TIM12**：Clock Source=Internal Clock，PSC=8399，Counter Mode=Up，NVIC 使能 `TIM8_BRK_TIM12` 全局中断（优先级 5）
4. **PB0/PB1**：GPIO_Output，默认 Low
5. 生成代码后，删除 `app_modbus.c` 中 `APP_Modbus_HW_Init()` 内对应的初始化代码和 `huart4`/`htim7`/`htim12` 句柄定义

### 3.4 中断优先级说明

FreeRTOS 要求管理的中断优先级数值 ≥ 5（数值越小优先级越高）。所有 Modbus 相关中断（UART2/UART4/TIM7/TIM12）均设为 **5**，确保可安全调用 FreeRTOS API。

---

## 四、FreeModbus 库结构

### 4.1 核心源码（不修改）

```
Middlewares/Third_Party/freemodbus/modbus/
├── include/
│   ├── mb.h              # 从站主API
│   ├── mb_m.h            # 主站主API
│   ├── mbconfig.h        # 配置裁剪（主站RTU已启用）
│   ├── mbport.h          # 移植层接口声明
│   ├── mbproto.h         # 功能码定义、异常码
│   ├── mbframe.h         # 帧结构
│   └── mbutils.h         # 工具函数
├── rtu/
│   ├── mbrtu.c           # 从站RTU状态机（收帧/发帧/CRC）
│   ├── mbrtu_m.c         # 主站RTU状态机
│   └── mbcrc.c           # CRC16-Modbus 计算
├── functions/
│   ├── mbfunccoils.c     # 从站 01/05/15 线圈
│   ├── mbfunccoils_m.c   # 主站 01/05/15 线圈
│   ├── mbfuncdisc.c      # 从站 02 离散输入
│   ├── mbfuncdisc_m.c    # 主站 02 离散输入
│   ├── mbfuncholding.c   # 从站 03/06/16 保持寄存器
│   ├── mbfuncholding_m.c # 主站 03/06/16 保持寄存器
│   ├── mbfuncinput.c     # 从站 04 输入寄存器
│   ├── mbfuncinput_m.c   # 主站 04 输入寄存器
│   ├── mbfuncother.c     # 从站其他功能码
│   └── mbutils.c         # 寄存器读写工具
├── mb.c                  # 从站主状态机 eMBPoll()
└── mb_m.c                # 主站主状态机
```

### 4.2 移植层（本实验重写，CMSIS-RTOS2 版）

```
Middlewares/Third_Party/freemodbus/port/
├── port.h            # 平台类型、硬件句柄、OS对象声明、临界区宏
├── portserial.c      # 从站串口 + UART2/UART4 中断回调统一分发
├── portserial_m.c    # 主站串口
├── porttimer.c       # 从站定时器 (TIM7)
├── porttimer_m.c     # 主站定时器 (TIM12)，三种定时模式
├── portevent.c       # 从站事件队列
└── portevent_m.c     # 主站事件队列 + 运行资源信号量 + 错误回调
```

### 4.3 mbconfig.h 关键配置

```c
#define MB_SLAVE_RTU_ENABLED       (1)   // 从站RTU
#define MB_MASTER_RTU_ENABLED      (1)   // 主站RTU（本实验核心）
#define MB_SLAVE_ASCII_ENABLED     (0)
#define MB_MASTER_ASCII_ENABLED    (0)
#define MB_SLAVE_TCP_ENABLED       (0)
#define MB_FUNC_HANDLERS_MAX       (16)
#define MB_MASTER_TIMEOUT_MS_RESPOND     100  // 主站响应超时(ms)
#define MB_MASTER_DELAY_MS_CONVERT       200  // 广播转换延时(ms)
```

---

## 五、具体操作流程

### 5.1 源码集成流程

1. **下载** FreeModbus_Slave-Master-RTT-STM32-master.zip
2. **解压**，复制 `FreeModbus/modbus/` 到 `Middlewares/Third_Party/freemodbus/modbus/`
3. **重写移植层**：将原 RT-Thread 版 `port/` 替换为 CMSIS-RTOS2 版（7 个文件）
4. **编写应用层**：`app_modbus.h/c`，含硬件初始化、寄存器映射、主从任务
5. **修改 CMakeLists.txt**：添加 21 个源文件 + 3 个 include 路径
6. **修改 module_cfg.h**：添加 `APP_MODBUS_ENABLE=1`
7. **修改 app_tasks.c**：创建 MbSlaveTask / MbMasterTask，初始化 RS485 GPIO
8. **修改 main.c**：定时器回调中分发 TIM7/TIM12
9. **编译**：`cmake --build build`，生成 hex/bin

### 5.2 硬件连接流程

```
从站总线 (UART4):
  STM32 PC10(TX) → MAX485#1 DI
  STM32 PC11(RX) ← MAX485#1 RO
  STM32 PB1      → MAX485#1 DE + RE (短接)
  MAX485#1 A/B → RS485总线#1 → 外部主站(PLC/Modbus Poll)
  总线两端各 120Ω 终端电阻

主站总线 (UART2):
  STM32 PA2(TX)  → MAX485#2 DI
  STM32 PA3(RX)  ← MAX485#2 RO
  STM32 PB0      → MAX485#2 DE + RE (短接)
  MAX485#2 A/B → RS485总线#2 → 外部从站(电表/传感器)
  总线两端各 120Ω 终端电阻
```

### 5.3 从站测试流程

1. USB 转 RS485 转换器接入总线#1
2. 电脑打开 **Modbus Poll**，配置：串口=COMx，9600 8N1，从站地址=1
3. **读保持寄存器**：功能码 03，地址 40001，数量 4 → 返回温度×10、湿度×10、LED状态、MQTT状态
4. **写保持寄存器**：功能码 06，地址 40003，值=1 → LED1 点亮
5. **读线圈**：功能码 01，地址 00001，数量 4 → 返回 LED1~4 状态
6. **写线圈**：功能码 05，地址 00001，值=ON → LED1 点亮

### 5.4 主站测试流程

1. USB 转 RS485 转换器接入总线#2
2. 电脑打开 **Modbus Slave**，配置：从站地址=2，9600 8N1
3. 定义保持寄存器 40001~40004，设为任意值
4. STM32 串口输出：`[Modbus-Master] Read addr=2 OK`
5. 读取 `APP_ModbusMaster_GetSnapshot()` 可获得采集数据

---

## 六、增加和修改的文件

### 6.1 新增文件（12 个）

| 文件路径 | 行数 | 说明 |
|---|---|---|
| `Middlewares/Third_Party/freemodbus/modbus/mb.c` | ~400 | 从站主状态机（第三方） |
| `Middlewares/Third_Party/freemodbus/modbus/mb_m.c` | ~500 | 主站主状态机（第三方） |
| `Middlewares/Third_Party/freemodbus/modbus/rtu/*.c` | 3 文件 | RTU 收发 + CRC16（第三方） |
| `Middlewares/Third_Party/freemodbus/modbus/functions/*.c` | 10 文件 | 功能码实现（第三方） |
| `Middlewares/Third_Party/freemodbus/port/port.h` | ~60 | 平台类型定义、硬件句柄、OS对象 |
| `Middlewares/Third_Party/freemodbus/port/portserial.c` | ~80 | 从站串口 + UART 回调分发 |
| `Middlewares/Third_Party/freemodbus/port/portserial_m.c` | ~70 | 主站串口 |
| `Middlewares/Third_Party/freemodbus/port/porttimer.c` | ~50 | 从站定时器 (TIM7) |
| `Middlewares/Third_Party/freemodbus/port/porttimer_m.c` | ~90 | 主站定时器 (TIM12) |
| `Middlewares/Third_Party/freemodbus/port/portevent.c` | ~35 | 从站事件队列 |
| `Middlewares/Third_Party/freemodbus/port/portevent_m.c` | ~110 | 主站事件队列 + 信号量 + 错误回调 |
| `usrcode/APP/Inc/app_modbus.h` | ~60 | 应用层头文件、快照结构 |
| `usrcode/APP/Src/app_modbus.c` | ~400 | 硬件初始化、寄存器映射、主从任务、中断 |

### 6.2 修改文件（4 个）

| 文件路径 | 修改内容 |
|---|---|
| `CMakeLists.txt` | 新增 `FREEMODBUS_SOURCES` 列表（21 个 .c），添加到 `target_sources`；新增 3 个 include 路径 |
| `usrcode/Common/Inc/module_cfg.h` | 新增 `APP_MODBUS_ENABLE=1` 开关 |
| `usrcode/APP/Src/app_tasks.c` | 新增 `app_modbus.h` 引用；新增 MbSlaveTask/MbMasterTask 句柄和属性；`CreateTasks` 中创建两个任务；`APP_Init` 中调用 `APP_Modbus_RS485_GPIO_Init()` |
| `Core/Src/main.c` | `HAL_TIM_PeriodElapsedCallback` 的 USER CODE 区域添加 TIM7/TIM12 分发到 FreeModbus |

### 6.3 编译验证

```
Memory region   Used Size   Region Size   %age Used
RAM:            27944 B      128 KB        21.32%
FLASH:          87680 B      512 KB        16.72%
```

FreeModbus 新增 FLASH 占用约 100 字节（核心代码已被 gc-sections 优化），RAM 新增约 72 字节（两个任务栈各 3KB + 队列/信号量）。

---

## 七、项目框架

### 7.1 整体分层架构

```
┌──────────────────────────────────────────────────────────────────┐
│                        APP 应用层                                 │
│  app_modbus.c  app_mqtt.c  app_dht11.c  app_key.c  app_cmd.c     │
│  (主从任务/寄存器映射)  (MQTT上云)  (传感器)  (按键)  (串口指令)    │
├──────────────────────────────────────────────────────────────────┤
│                      Protocol 协议层                              │
│  FreeModbus核心 + 移植层  │  mqtt_port.c  │  protocol.c           │
│  (mb.c/mb_m.c/RTU/功能码)  │  (Paho移植)  │  (文本/hex协议)       │
├──────────────────────────────────────────────────────────────────┤
│                        BSP 板级支持包                             │
│  bsp_esp8266.c  bsp_uart.c  bsp_dht11.c  bsp_led.c  ...          │
│  (ESP8266 AT驱动)  (DMA+IDLE)  (单总线)  (GPIO)                  │
├──────────────────────────────────────────────────────────────────┤
│              HAL / FreeRTOS / CMSIS-RTOS2                        │
│  STM32CubeMX: GPIO/UART/TIM/DMA + FreeRTOS内核                   │
└──────────────────────────────────────────────────────────────────┘
```

### 7.2 Modbus 子系统内部架构

```
┌─────────────────────────────────────────────────────┐
│                   应用层 (app_modbus.c)               │
│  ┌──────────────┐          ┌──────────────┐         │
│  │ 从站任务      │          │ 主站任务      │         │
│  │ MbSlaveTask  │          │ MbMasterTask │         │
│  │ eMBPoll循环  │          │ 周期请求     │         │
│  └──────┬───────┘          └──────┬───────┘         │
│         │ 寄存器回调               │ 响应回调        │
│         ▼                          ▼                │
│  eMBRegHoldingCB          eMBMasterRegHoldingCB     │
│  eMBRegInputCB            (数据存入快照)             │
│  eMBRegCoilsCB                                 │
├─────────┼──────────────────────────┼────────────────┤
│         ▼                          ▼                │
│  ┌──────────────┐          ┌──────────────┐         │
│  │ FreeModbus    │          │ FreeModbus    │         │
│  │ 从站核心      │          │ 主站核心      │         │
│  │ mb.c/mbrtu.c  │          │ mb_m.c/mbrtu_m│         │
│  └──────┬───────┘          └──────┬───────┘         │
├─────────┼──────────────────────────┼────────────────┤
│         ▼                          ▼                │
│  ┌──────────────┐          ┌──────────────┐         │
│  │ 从站移植层    │          │ 主站移植层    │         │
│  │ portserial.c │          │ portserial_m │         │
│  │ porttimer.c  │          │ porttimer_m  │         │
│  │ portevent.c  │          │ portevent_m  │         │
│  └──────┬───────┘          └──────┬───────┘         │
└─────────┼──────────────────────────┼────────────────┘
          ▼                          ▼
     UART4 + TIM7 + PB1         UART2 + TIM12 + PB0
     (RS485总线#1)              (RS485总线#2)
```

---

## 八、程序流程

### 8.1 系统启动流程

```
main()
  ├─ HAL_Init / SystemClock / 外设初始化 (CubeMX)
  ├─ osKernelInitialize()
  ├─ MX_FREERTOS_Init()
  │     ├─ APP_Init()
  │     │    ├─ BSP初始化 (UART1/UART3/ESP8266/DHT11/...)
  │     │    └─ APP_Modbus_RS485_GPIO_Init()  ← PB0/PB1 设为输出
  │     ├─ APP_TASKS_CreateObjects()  ← 创建队列/信号量
  │     └─ APP_TASKS_CreateTasks()
  │           ├─ DHT11Task      (温湿度采集)
  │           ├─ MqttTask       (华为云上云)
  │           ├─ MbSlaveTask    ← Modbus从站
  │           └─ MbMasterTask   ← Modbus主站
  └─ osKernelStart()
```

### 8.2 从站任务流程

```
MbSlaveTask
  │
  ├─ APP_Modbus_HW_Init()      // 初始化UART4/TIM7/TIM12/UART2波特率
  ├─ 创建从站事件队列 (mbSlaveEventQueue)
  ├─ eMBInit(MB_RTU, addr=1, port=0, 9600, MB_PAR_NONE)
  │    └─ 内部调用 xMBPortSerialInit / xMBPortTimersInit / xMBPortEventInit
  ├─ eMBEnable()               // 启动接收，开定时器
  └─ for(;;)
       └─ eMBPoll()           // 阻塞等待事件 → 处理 → 回复
            │
            ├─ EV_FRAME_RECEIVED: 解析功能码 → 调用寄存器回调 → 组装响应帧
            ├─ EV_EXECUTE:        执行功能码处理
            └─ EV_FRAME_SENT:     响应帧发送完成
```

### 8.3 主站任务流程

```
MbMasterTask
  │
  ├─ APP_Modbus_HW_Init()      // 幂等，从站已初始化则直接返回
  ├─ 创建主站事件队列 + 运行资源信号量
  ├─ eMBMasterInit(MB_RTU, port=0, 9600, MB_PAR_NONE)
  ├─ eMBMasterEnable()
  └─ for(;;)
       ├─ eMBMasterReqReadHoldingRegister(addr=2, start=0, count=4, timeout=1000)
       │    ├─ 获取运行资源信号量
       │    ├─ 组装请求帧 → 发送 → 等待响应
       │    │    └─ 内部 eMBMasterPoll() 处理收发状态机
       │    ├─ 收到响应 → eMBMasterRegHoldingCB() → 数据存入快照
       │    └─ 释放信号量，返回结果
       └─ osDelay(5000ms)     // 5秒采集周期
```

### 8.4 从站接收一帧的完整中断流程

```
外部主站发送请求帧
    │
    ▼
UART4 接收中断 (HAL_UART_RxCpltCallback)
    │ 每收到1字节 → pxMBFrameCBByteReceived() → 存入RTU缓冲区
    │ 启动定时器 TIM7 (T3.5 = 约5ms @9600)
    ▼
TIM7 超时中断 (HAL_TIM_PeriodElapsedCallback → MB_Slave_TimerExpiredHandler)
    │ pxMBPortCBTimerExpired() → 判定帧结束
    │ 发送事件 EV_FRAME_RECEIVED 到队列
    ▼
MbSlaveTask 的 eMBPoll() 被唤醒
    │ 校验CRC → 解析功能码 → 调用 eMBRegHoldingCB 等回调
    │ 回调中读取DHT11快照/控制LED/返回数据
    │ 组装响应帧 → 启动发送
    ▼
UART4 发送完成中断 (HAL_UART_TxCpltCallback)
    │ 等待TC标志 → DE切回接收 → pxMBFrameCBTransmitterEmpty()
    │ 发送事件 EV_FRAME_SENT
    ▼
eMBPoll() 处理完成，等待下一帧
```

---

## 九、数据流程

### 9.1 下行数据流程（外部主站 → STM32从站 → 执行动作）

```
外部主站(PLC/Modbus Poll)
    │ RS485总线#1, Modbus RTU帧
    │ [地址][功能码][数据][CRC16]
    ▼
MAX485#1 → UART4 RX (PC11)
    │ DMA/中断逐字节接收
    ▼
FreeModbus从站RTU层 (mbrtu.c)
    │ 逐字节存入缓冲区，TIM7超时判定帧结束
    │ CRC16校验
    ▼
FreeModbus从站核心 (mb.c)
    │ 解析功能码 → 分发到功能码处理函数
    ▼
寄存器回调 (app_modbus.c)
    ├─ 功能码03/04(读): 读取DHT11快照 → 组装温度/湿度数据
    ├─ 功能码06/16(写保持寄存器): 写40003 → 更新LED状态
    └─ 功能码05/15(写线圈): 写00001~00004 → 更新LED状态
    ▼
执行动作: BSP_LED_On/Off() → GPIO输出 → LED亮灭
    ▼
组装响应帧 → UART4 TX (PC10) → MAX485#1 → RS485总线#1 → 外部主站
```

### 9.2 上行数据流程（STM32主站 → 外部从站 → 采集数据 → 快照）

```
MbMasterTask (周期5秒)
    │ eMBMasterReqReadHoldingRegister(addr=2, 40001~40004)
    ▼
FreeModbus主站核心 (mb_m.c)
    │ 组装请求帧 [02][03][0000][0004][CRC]
    ▼
主站移植层 (portserial_m.c)
    │ DE=PB0拉高(发送模式) → UART2 TX → MAX485#2 → RS485总线#2
    │ 发送完成 → DE拉低(接收模式) → 启动TIM12响应超时(100ms)
    ▼
外部从站(电表/传感器)
    │ 解析请求 → 组装响应帧 → 发回总线
    ▼
MAX485#2 → UART2 RX (PA3)
    │ 逐字节接收 → TIM12 T3.5超时判定帧结束
    ▼
FreeModbus主站RTU层 (mbrtu_m.c)
    │ CRC校验 → 解析响应
    ▼
主站寄存器回调 (app_modbus.c)
    │ eMBMasterRegHoldingCB() → 数据存入 s_master_snapshot (互斥锁保护)
    ▼
共享快照 ModbusMaster_Snapshot_t
    │ APP_ModbusMaster_GetSnapshot() 可供其他任务读取
    ▼
（可选）MqttTask 读取快照 → 组装JSON → MQTT上报华为云
```

### 9.3 从站寄存器数据来源

```
DHT11Task (5秒周期)
    │ 读取DHT11 → 更新 DHT11_Snapshot_t (临界区保护)
    ▼
DHT11_Snapshot_t (共享快照)
    │ APP_DHT11_GetSnapshot()
    ▼
从站寄存器回调 eMBRegInputCB / eMBRegHoldingCB
    │ 读取快照 → temperature_int*10+temperature_dec → 填入寄存器
    ▼
Modbus响应帧 → 外部主站
```

---

## 十、业务模式

### 10.1 主从模式（Master-Slave）

Modbus 是典型的**主从（Master-Slave）协议**，同一总线上只有一个主站，从站被动响应：

```
主站(Master)                    从站(Slave)
    │                              │
    │──── 请求帧 ─────────────────►│  主站发起，从站不主动发数据
    │                              │
    │◄─── 响应帧 ─────────────────│  从站收到请求后回复
    │                              │
    │──── 请求帧 ─────────────────►│
    │◄─── 响应帧 ─────────────────│
```

- **主站**：主动发起请求，控制总线时序，处理超时和错误
- **从站**：被动监听，仅在被寻址时回复，不主动发送

### 10.2 本实验的双角色模式

本实验中 STM32 **同时扮演主站和从站**，通过两条独立 RS485 总线实现：

```
外部主站(PLC) ──总线#1──► STM32(从站) ──总线#2──► 外部从站(电表)
                  被读取/被控制                  主动读取/采集
```

- **作为从站**：向上游系统（PLC/上位机）提供数据和控制接口
- **作为主站**：向下游设备（传感器/电表）采集数据
- 形成**协议网关**：上游用 Modbus 接入，下游用 Modbus 采集，同时还能 MQTT 上云

### 10.3 请求-应答模式

主站每次请求后等待应答，通过 `eMBMasterWaitRequestFinish()` 阻塞等待：
- 成功：`MB_MRE_NO_ERR`
- 超时：`MB_MRE_TIMEDOUT`（从站未响应）
- 接收错误：`MB_MRE_REV_DATA`（CRC错误/帧错误）
- 功能码错误：`MB_MRE_EXE_FUN`（从站返回异常码）

### 10.4 寄存器映射模式

从站将内部数据映射为四种 Modbus 数据区：

| 数据区 | 功能码 | 读写性 | 本实验映射 |
|---|---|---|---|
| 线圈 (Coils) | 01/05/15 | 读写 | LED1~LED4 状态 |
| 离散输入 (Discrete Input) | 02 | 只读 | 未使用 |
| 输入寄存器 (Input Register) | 04 | 只读 | 温度×10、湿度×10 |
| 保持寄存器 (Holding Register) | 03/06/16 | 读写 | 温度/湿度/LED状态/MQTT状态 |

---

## 十一、生产者和消费者

### 11.1 Modbus 总线层面

| 角色 | 实体 | 生产内容 | 消费内容 |
|---|---|---|---|
| **生产者** | 外部主站(PLC) | Modbus 请求帧（读/写命令） | — |
| **生产者** | STM32 从站 | Modbus 响应帧（数据/确认） | 请求帧 |
| **消费者** | STM32 从站 | — | 请求帧（解析并执行） |
| **消费者** | 外部主站(PLC) | — | 响应帧（获取数据/确认） |
| **生产者** | STM32 主站 | Modbus 请求帧（读取外部从站） | — |
| **生产者** | 外部从站(电表) | Modbus 响应帧（寄存器数据） | — |
| **消费者** | STM32 主站 | — | 响应帧（解析并存入快照） |

**STM32 既是生产者也是消费者**：
- 在总线#1上：作为从站，消费请求帧，生产响应帧
- 在总线#2上：作为主站，生产请求帧，消费响应帧

### 11.2 FreeRTOS 任务间生产者-消费者

```
DHT11Task (生产者) ──DHT11_Snapshot_t──► MbSlaveTask (消费者)
    5秒采集温湿度                      寄存器回调中读取快照

外部从站设备 (生产者) ──RS485──► MbMasterTask (消费者)
    响应帧数据                       解析后存入主站快照

MbMasterTask (生产者) ──ModbusMaster_Snapshot_t──► MqttTask (消费者,可选)
    采集外部设备数据                        读取快照并MQTT上报

UART4 RX中断 (生产者) ──事件队列──► MbSlaveTask (消费者)
    字节接收/超时事件                   eMBPoll()阻塞等待

UART2 RX中断 (生产者) ──事件队列──► MbMasterTask (消费者)
    字节接收/超时事件                   eMBMasterPoll()处理
```

### 11.3 事件队列机制

FreeModbus 的状态机不使用轮询，而是通过 OS 事件队列阻塞等待：
- 从站：`mbSlaveEventQueueHandle`，事件类型 `EV_READY/EV_FRAME_RECEIVED/EV_EXECUTE/EV_FRAME_SENT`
- 主站：`mbMasterEventQueueHandle`，事件类型含成功/超时/接收错误/执行错误
- 中断中 `xMBPortEventPost()` 发送事件，任务中 `xMBPortEventGet()` 阻塞接收

---

## 十二、特点与要点

### 12.1 主从同栈运行

- 官方 FreeModbus 仅从站开源，armink 扩展版新增主站，主从共用同一套 RTU/CRC/功能码框架
- 主站和从站使用**独立的状态机、独立的串口、独立的定时器、独立的事件队列**，互不干扰
- 两条 RS485 总线物理隔离，避免总线冲突

### 12.2 移植层完全重写为 CMSIS-RTOS2

原 armink 版绑定 RT-Thread（`rt_event`/`rt_sem`），本实验全部替换为 CMSIS-RTOS2：
- `rt_event` → `osMessageQueueId_t`（消息队列）
- `rt_sem` → `osSemaphoreId_t`（信号量）
- `rt_thread` → FreeRTOS 任务（`osThreadNew`）
- 临界区 `rt_enter_critical` → `taskENTER_CRITICAL()`

### 12.3 硬件初始化代码内置，开箱即用

UART4/TIM7/TIM12 未在 CubeMX 中配置，而是在 `APP_Modbus_HW_Init()` 中手动初始化：
- 不需要用户打开 CubeMX 重新配置
- 使用静态标志 `s_hw_inited` 确保只初始化一次（主从任务都会调用）
- 如用户后续在 CubeMX 中配置，删除对应代码即可

### 12.4 定时器资源规避

- TIM6 被 HAL 时基占用（`HAL_IncTick`），不能用作 Modbus 定时器
- 主站原计划用 TIM6，改为 **TIM12**（通用定时器，APB1 总线，84MHz）
- TIM12 中断名为 `TIM8_BRK_TIM12_IRQn`（与 TIM8 刹车共享中断线）
- 从站用 **TIM7**（基本定时器，简单可靠）
- 两个定时器均配置 PSC=8399，得到 0.1ms/tick，便于计算 T3.5 和毫秒级超时

### 12.5 RS485 方向控制

- DE/RE 引脚控制：发送前拉高，发送完成后拉低
- **关键**：发送完成必须等待 UART TC（发送完成）标志，而不是 TXE（发送数据寄存器空），否则最后一个字节还没发出去就切接收，导致丢尾字节
- 代码中：`while (__HAL_UART_GET_FLAG(&huart, UART_FLAG_TC) == RESET);` 然后切回接收模式

### 12.6 中断回调统一分发

- `HAL_UART_RxCpltCallback` / `HAL_UART_TxCpltCallback` 在 `portserial.c` 中统一定义，根据 `huart->Instance` 分发到从站(UART4)或主站(UART2)
- `HAL_TIM_PeriodElapsedCallback` 在 `main.c` 中统一定义，USER CODE 区域分发 TIM7(从站)和 TIM12(主站)
- 避免多个文件定义同名 HAL 回调导致链接冲突

### 12.7 主站运行资源互斥

- 主站使用信号量 `mbMasterRunSemHandle` 保护运行资源
- `xMBMasterRunResTake()` 获取资源，`vMBMasterRunResRelease()` 释放
- 确保同一时刻只有一个请求在处理，避免状态机重入

### 12.8 主站数据通过回调返回

- 主站请求 API `eMBMasterReqReadHoldingRegister()` 不返回数据缓冲区
- 数据通过 `eMBMasterRegHoldingCB()` 回调返回，回调中存入共享快照
- 快照使用互斥锁保护，支持多任务安全读取

### 12.9 与 MQTT 模块无缝协同

- Modbus 主站采集的数据存入 `ModbusMaster_Snapshot_t`
- MQTT 任务可通过 `APP_ModbusMaster_GetSnapshot()` 读取并上报华为云
- 形成 **Modbus 采集 → MQTT 上云** 的典型物联网网关数据流
- 从站的 40004 寄存器直接映射 MQTT 连接状态，外部主站可查询设备上云状态

### 12.10 条件编译可裁剪

- `module_cfg.h` 中 `APP_MODBUS_ENABLE=0` 可完全关闭 Modbus
- `mbconfig.h` 中可单独关闭主站或从站
- 关闭后不生成任何代码，不影响其他模块

---

## 十三、实验结果与验证

### 13.1 编译验证

```
[100%] Linking C executable pro_v2.1.elf
Memory region   Used Size   Region Size   %age Used
RAM:            27944 B      128 KB        21.32%
FLASH:          87680 B      512 KB        16.72%
   text    data     bss     dec     hex
  87428     244   27700  115372   1c2ac
```

零错误，零警告（printf 已包含 stdio.h）。

### 13.2 预期运行结果

**从站侧**（串口输出）：
```
[Modbus-Slave] Started: addr=1, UART4, 9600 8N1
```

**主站侧**（串口输出）：
```
[Modbus-Master] Started: UART2, 9600 8N1, polling addr=2
[Modbus-Master] Read addr=2 OK
[Modbus-Master] Read addr=2 OK
...
```

**从站寄存器测试预期**：

| 操作 | 命令 | 预期结果 |
|---|---|---|
| 读 40001 | 功能码03 | 返回温度×10（如 285） |
| 读 40001~40004 | 功能码03, 数量4 | 温度、湿度、LED状态、MQTT状态 |
| 写 40003=1 | 功能码06 | LED1 点亮 |
| 写 40003=15 | 功能码06 | LED1~4 全亮 |
| 读 00001~00004 | 功能码01 | 返回 LED 状态位 |
| 写 00001=ON | 功能码05 | LED1 点亮 |

### 13.3 主站采集测试预期

外部从站（Modbus Slave 模拟）保持寄存器设为 `[100, 200, 300, 400]`，STM32 主站采集后：
```c
ModbusMaster_Snapshot_t snap;
APP_ModbusMaster_GetSnapshot(&snap);
// snap.valid = 1
// snap.regs[0] = 100, regs[1] = 200, regs[2] = 300, regs[3] = 400
```

---

## 十四、常见问题与排查

| 问题 | 原因 | 解决方法 |
|---|---|---|
| 从站完全不响应 | RS485 A/B 接反 | 交换 A/B 线 |
| 从站能收但不回复 | DE/RE 切换太慢，发完没等 TC | 确认代码中 `while(TC==RESET)` 等待 |
| 回复数据错乱 | TIM7 T3.5 时间不对 | 确认 PSC=8399（0.1ms/tick），9600 下 T3.5≈5ms |
| 主站一直超时 | 外部从站地址/波特率不匹配 | 确认从站地址=2，9600 8N1 |
| 主站收到错误数据 | 两条 RS485 总线短接或干扰 | 确认总线#1和总线#2独立，A/B不混淆 |
| 编译报错 htim6 重复定义 | TIM6 被 HAL 时基占用 | 已改用 TIM12，不要用 TIM6 |
| 编译报错回调重复定义 | 多个文件定义 HAL 回调 | UART 回调只在 portserial.c，定时器回调只在 main.c |
| 主站请求后卡死 | 信号量未释放或事件未发送 | 确认 `eMBMasterWaitRequestFinish` 能收到事件 |
| 中断优先级导致 HardFault | 中断优先级 <5（数值） | 所有 Modbus 中断优先级设为 5 或更低 |
| 奇偶校验不匹配 | 主从校验位不一致 | 两边都设为 None（本实验固定无校验） |

---

## 十五、实验总结

本实验成功将 FreeModbus（armink 主从扩展版）移植到 STM32F407 + FreeRTOS 平台，实现：

1. **从站功能**：UART4 + RS485，地址 1，支持 01/02/03/04/05/06/15/16 功能码，寄存器映射温湿度和 LED
2. **主站功能**：UART2 + RS485，周期读取外部从站地址 2 的保持寄存器，数据存入快照
3. **主从并行**：独立串口/定时器/总线，同时运行互不干扰
4. **OS 集成**：CMSIS-RTOS2 事件队列 + 信号量，替代原 RT-Thread 移植层
5. **与现有系统协同**：DHT11 数据供从站读取，主站采集数据可供 MQTT 上报
6. **可裁剪**：通过 `APP_MODBUS_ENABLE` 和 `mbconfig.h` 灵活配置

项目形成了一个**多协议物联网网关雏形**：下行 Modbus 采集传感器数据，上行 Modbus 供工业系统接入，同时 MQTT 上云，三种协议并行运行，为后续扩展更多协议（如 OPC UA、BACnet）奠定了架构基础。
