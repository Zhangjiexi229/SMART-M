# 华为云 IoTDA MQTT 接入详细配置指南

> 适用项目：STM32F407 + ESP8266 + Paho MQTT
> 文档版本：2026-09

---

## 第一步：开通华为云设备接入服务

1. 登录 [华为云控制台](https://console.huaweicloud.com/)
2. 顶部搜索框输入 **"设备接入"**，进入 **IoTDA 设备接入** 服务
3. 点击 **"开通服务"**（首次使用）
4. 选择 **"标准版"** → 实例规格选 **"免费单元 S0"**
   - 免费额度：1000 设备注册、10000 条消息/日、10 TPS
   - 完全免费，无需付费 ["https://support.huaweicloud.com/productdesc-iothub/iot_04_0014.html"]
5. 勾选同意协议 → 点击 **"立即开通"**
6. 开通后进入实例控制台

---

## 第二步：创建产品

1. 左侧导航栏点击 **"产品"**
2. 点击右上角 **"创建产品"**
3. 填写参数：

| 参数 | 填写值 | 说明 |
|---|---|---|
| 产品名称 | `STM32_Sensor` | 自定义，资源空间内唯一 |
| 协议类型 | **MQTT** | 必须选 MQTT |
| 数据格式 | **JSON** | 物模型数据格式 |
| 设备类型 | **自定义类型** → `Sensor` | 可自定义 |
| 厂商名称 | `Developer` | 任意填写 |

4. 点击 **"确定"**，产品创建成功

---

## 第三步：定义物模型（产品功能定义）

物模型是华为云的核心概念，定义了设备有哪些属性（上报）和哪些命令（下行）。

### 3.1 添加服务

1. 进入刚创建的产品 → 点击 **"功能定义"** 标签页
2. 点击 **"添加服务"**
   - 服务 ID：`Sensor`（重要，代码中会用到）
   - 服务类型：自定义
3. 点击确定

### 3.2 添加属性（上报用）

在 `Sensor` 服务下点击 **"添加属性"**，依次添加：

**属性1：温度**
| 参数 | 值 |
|---|---|
| 属性名称 | `temperature` |
| 数据类型 | `decimal`（浮点型） |
| 取值范围 | `-40 ~ 80` |
| 步长 | `0.1` |
| 单位 | `℃` |

**属性2：湿度**
| 参数 | 值 |
|---|---|
| 属性名称 | `humidity` |
| 数据类型 | `decimal` |
| 取值范围 | `0 ~ 100` |
| 步长 | `0.1` |
| 单位 | `%RH` |

### 3.3 添加命令（下行控制用）

在 `Sensor` 服务下点击 **"添加命令"**：

| 参数 | 值 |
|---|---|
| 命令名称 | `LED_Control` |
| 命令描述 | `控制板载LED` |

点击确定后，在命令下点击 **"添加输入参数"**：

| 参数 | 值 |
|---|---|
| 参数名称 | `led` |
| 数据类型 | `int` |
| 取值范围 | `0 ~ 15` |
| 说明 | `bit0=LED1, bit1=LED2, bit2=LED3, bit3=LED4` |

> 也可以添加多个命令（如 LED1_ON、LED1_OFF），但用一个命令 + 参数更简洁。

---

## 第四步：注册设备

1. 左侧导航栏点击 **"设备"**
2. 点击右上角 **"添加设备"**
3. 填写参数：

| 参数 | 填写值 |
|---|---|
| 所属产品 | 选择刚才创建的 `STM32_Sensor` |
| 设备标识码 | `stm32_01`（自定义，即 node_id） |
| 设备名称 | `stm32_01` |
| 认证类型 | **密钥** |
| 密钥 | 留空（平台自动生成） |

4. 点击 **"确定"**
5. **重要**：弹窗会显示设备信息，立即保存以下两个值：
   - **设备ID（device_id）**：类似 `66d2a1b3c4d5e6f7a8b9c0d1_stm32_01`
   - **设备密钥（device_secret）**：类似 `abcdef1234567890abcdef1234567890`

> 这两个值后面生成 MQTT 鉴权参数必须用到，关闭弹窗后密钥不再显示。

---

## 第五步：获取 MQTT 接入地址

1. 左侧导航栏点击 **"总览"**
2. 在 **"接入信息"** 区域找到 **"MQTT 接入地址"**
3. 复制地址，格式类似：
   ```
   xxxxxxxx.st1.iotda-app.cn-north-4.myhuaweicloud.com
   ```
4. 端口：**1883**（非加密）或 **8883**（TLS 加密，本项目用 1883）

---

## 第六步：生成 MQTT 鉴权参数（关键步骤）

华为云的 MQTT 密码不是明文，需要用 HMAC-SHA256 签名。**用官方在线工具生成最简单**：

1. 打开华为云官方生成工具：
   **https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/** ["https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/"]

2. 填写参数：
   - **认证类型**：密钥认证
   - **密码签名类型**：**不校验时间戳**（选这个，生成一次永久有效，适合嵌入式硬编码）
   - **DeviceId**：粘贴第四步的设备ID
   - **DeviceSecret**：粘贴第四步的设备密钥

3. 点击生成，得到三个值：

| 生成结果 | 对应 MQTT 参数 | 示例 |
|---|---|---|
| ClientId | MQTT Client ID | `66d2..._stm32_01_0_0_2026090402` |
| Username | MQTT 用户名 | `66d2..._stm32_01`（即 device_id） |
| Password | MQTT 密码 | `a1b2c3d4...`（64位十六进制） |

4. **保存这三个值**，后面填入代码

> 为什么选"不校验时间戳"：嵌入式设备没有 RTC 或时间不准，校验时间戳会导致连接失败。不校验时间戳的密码生成一次可长期使用，适合实验和原型。

---

## 第七步：MQTT 主题与数据格式

### 7.1 主题一览

| 方向 | 主题 | 说明 |
|---|---|---|
| 设备→云（发布） | `$oc/devices/{device_id}/sys/properties/report` | 属性上报 |
| 云→设备（订阅） | `$oc/devices/{device_id}/sys/commands/#` | 命令下发（#通配 request_id） |
| 设备→云（发布） | `$oc/devices/{device_id}/sys/commands/response/request_id={request_id}` | 命令响应 |

> `{device_id}` 替换为第四步的实际设备ID

### 7.2 属性上报 JSON 格式

```json
{
  "services": [{
    "service_id": "Sensor",
    "properties": {
      "temperature": 25.5,
      "humidity": 60.0
    }
  }]
}
```

- `service_id` 必须与第三步定义的服务ID一致（本例为 `Sensor`）
- `properties` 中的键名必须与定义的属性名一致（`temperature`、`humidity`）
- 可选 `event_time` 字段，不带则用平台时间 ["https://support.huaweicloud.com/api-iothub/iot_06_v5_3010.html"]

### 7.3 命令下发 JSON 格式（云→设备）

平台下发命令时，设备收到的 payload：

```json
{
  "request_id": "42aa08ea-84c1-4025-a7b2-c1f6efe547c2",
  "service_id": "Sensor",
  "command_name": "LED_Control",
  "paras": {
    "led": 1
  }
}
```

- `request_id`：必须在响应中原样返回 ["https://support.huaweicloud.com/api-iothub/iot_06_v5_3014.html"]
- `command_name`：命令名称，与物模型定义一致
- `paras`：命令参数

### 7.4 命令响应 JSON 格式（设备→云）

```json
{
  "result_code": 0,
  "response_name": "COMMAND_RESPONSE",
  "paras": {
    "result": "success"
  }
}
```

- `result_code`：0 表示成功，非 0 表示失败 ["https://support.huaweicloud.com/intl/zh-cn/usermanual-iothub/iot_01_0339.html"]

---

## 第八步：代码修改（已自动完成，见下）

项目代码已修改为华为云版本，需修改的文件：

1. **`usrcode/APP/Inc/app_mqtt.h`** — 填入你的 Broker 地址、ClientId、Username、Password、设备ID
2. **`usrcode/APP/Src/app_mqtt.c`** — JSON 上报格式已改为华为云物模型格式，下行命令已改为 JSON 解析

### 8.1 需要你手动填入的参数

打开 `app_mqtt.h`，修改以下 5 个值：

```c
#define MQTT_BROKER_HOST      "你的接入地址"      // 第五步获取
#define MQTT_CLIENT_ID        "你的ClientId"      // 第六步生成
#define MQTT_USERNAME         "你的Username"      // 第六步生成（即device_id）
#define MQTT_PASSWORD         "你的Password"      // 第六步生成
#define HUAWEI_DEVICE_ID      "你的设备ID"        // 第四步获取（用于主题拼接）
```

### 8.2 代码已自动完成的修改

- 上报主题：`$oc/devices/{device_id}/sys/properties/report`
- 订阅主题：`$oc/devices/{device_id}/sys/commands/#`
- 响应主题：`$oc/devices/{device_id}/sys/commands/response/request_id={request_id}`
- JSON 上报：华为云 `services` 格式
- 下行解析：从 JSON 中提取 `request_id`、`command_name`、`paras.led`，执行 LED 控制后回复响应

---

## 第九步：编译烧录与验证

### 9.1 编译烧录

1. 修改 `app_mqtt.h` 中的 5 个参数
2. Build 项目
3. 烧录到 STM32

### 9.2 串口验证

打开串口调试助手（115200），复位后应看到：

```
[MQTT] Task started, broker=xxx.iotda-app.cn-north-4.myhuaweicloud.com:1883, publish every 3000ms
[MQTT] ===== Connecting to cloud =====
[MQTT] AT test...
[MQTT] AT OK
[MQTT] Joining AP "spring" ...
[MQTT] WiFi connected
[MQTT] TCP connecting xxx.iotda-app.cn-north-4.myhuaweicloud.com:1883 ...
[MQTT] TCP connected
[MQTT] MQTT connected (client=xxx_0_0_2026090402)
[MQTT] Subscribed to "$oc/devices/xxx/sys/commands/#" (QoS=0)
[MQTT] ===== All connected, start publishing =====
```

### 9.3 查看上报数据

1. 华为云控制台 → 左侧 **"设备"** → 点击你的设备
2. 点击 **"属性"** 标签页
3. 应能看到 `temperature` 和 `humidity` 每 3 秒更新一次

### 9.4 下发命令测试

1. 华为云控制台 → 左侧 **"设备"** → 点击你的设备
2. 点击 **"命令"** 标签页
3. 选择命令 `LED_Control`，参数 `led` 填 `1`（点亮 LED1）
4. 点击 **"下发"**
5. 观察开发板 LED1 是否点亮，串口应打印：
   ```
   [MQTT] RECV topic=$oc/devices/xxx/sys/commands/request_id=xxx len=...
   [MQTT-CMD] Huawei command: LED_Control, led=1
   [MQTT] Command response sent
   ```
6. 控制台命令状态变为 **"已送达"**，响应显示 `result_code: 0`

---

## 第十步：常见问题排查

| 问题 | 原因 | 解决方法 |
|---|---|---|
| MQTT CONNECT FAILED rc=-1 | 鉴权参数错误 | 重新用在线工具生成 ClientId/Username/Password，确认选了"不校验时间戳" |
| MQTT CONNECT FAILED rc=-2 | Broker 地址/端口错误 | 检查接入地址，确认端口 1883 |
| TCP connect FAILED | 域名无法解析 | ESP8266 不支持某些域名，可 ping 域名获取 IP 后填 IP |
| 属性上报后平台看不到数据 | service_id 或属性名不匹配 | 检查代码中的 `service_id` 和属性名是否与物模型完全一致 |
| 命令下发设备没反应 | 订阅主题错误 | 确认订阅主题是 `$oc/devices/{device_id}/sys/commands/#` |
| 命令状态一直"等待响应" | 没发响应或 request_id 不对 | 确认响应主题中包含正确的 request_id |

---

## 附：华为云 vs 公共 Broker（emqx）对比

| 项目 | 公共 Broker (emqx) | 华为云 IoTDA |
|---|---|---|
| 鉴权 | 无（匿名） | 密钥 + HMAC-SHA256 |
| 数据格式 | 自由 JSON | 物模型规范 JSON |
| 设备管理 | 无 | 产品/设备/属性/命令管理 |
| 数据存储 | 无（转发即丢） | 平台存储历史数据 |
| 可视化 | 需自己做 | 控制台直接看属性/命令 |
| 适合场景 | 调试、原型 | 正式项目、毕设 |
