# STM32F407VET6 核心板 — SD 卡数据存储（SDIO 4-bit + FatFs）

> 适用工程：`D:\STM32kdl\SMART-M`（工业电机监测系统，FreeRTOS）
> 方案：板载 U6 MiniSD（TF 卡）插座 + SDIO 4-bit + FatFs R0.12c，周期把诊断快照写成 CSV 日志落盘。
> 编译：已通过（Debug preset），产物 `build/Debug/SMART-M.elf / .bin / .hex`。

---

## 1. 硬件接线（板上已布好，无需飞线）

U6 是板上已焊接的 MiniSD 插座，直接连到 MCU 的 SDIO 外设，信号全部为**板上走线**：

| U6 MiniSD 引脚 | 插座丝印 | 网络标号 | MCU 引脚 | 说明 |
| --- | --- | --- | --- | --- |
| 1 | DAT2 | SDIO_D2 | PC10 | 数据线 2 |
| 2 | DAT3 | SDIO_D3 | PC11 | 数据线 3 |
| 3 | CMD | SDIO_CMD | PD2 | 命令线 |
| 4 | VDD | 3V3 | — | 卡供电（并 C12 0.1 µF 去耦） |
| 5 | CLK | SDIO_SCK | PC12 | 时钟 |
| 6 | GND | GND | — | 地 |
| 7 | DAT0 | SDIO_D0 | PC8 | 数据线 0 |
| 8 | DAT1 | SDIO_D1 | PC9 | 数据线 1 |
| 9 | NC | — | — | 未用 |

- 供电：3V3 + 去耦电容 C12（0.1 µF）。
- 原理图上 SDIO 信号**无外部上拉电阻**。bsp_sd.c 的 MSP 初始化已对 CMD/D0–D3 开启 MCU 内部上拉，可以工作；**若遇到不稳定，建议在 CMD/D0–D3 上加 10 kΩ 外部上拉到 3V3**（板上预留焊盘或飞线）。

## 2. 软件实现

### 2.1 新增文件

| 文件 | 作用 |
| --- | --- |
| `Middlewares/Third_Party/FatFs/src/ff.c / ff.h / diskio.h / integer.h / ffconf_template.h` | FatFs R0.12c 官方源码（复制自 STM32Cube_FW_F4_V1.28.3） |
| `Middlewares/Third_Party/FatFs/src/ffconf.h` | FatFs 配置（LFN 开启、码页 437、无 RTC、卷数 1） |
| `Middlewares/Third_Party/FatFs/src/diskio.c` | 磁盘对接层：SD 卡 ↔ FatFs，扇区 512B，含 512B 对齐缓冲中转 |
| `Middlewares/Third_Party/FatFs/src/option/` | 码页转换表（437），由 `unicode.c` 按码页自动引入 |
| `Drivers/STM32F4xx_HAL_Driver/Src/Inc/stm32f4xx_hal_sd.c/.h`、`stm32f4xx_ll_sdmmc.c/.h` | ST 官方 HAL SD/LL 驱动（V1.28.3） |
| `usrcode/04_bsp/Inc/bsp_sd.h` + `Src/bsp_sd.c` | SDIO 4-bit 轮询驱动：GPIO AF12 + 内部上拉、400 kHz 识别、24 MHz 高速、读写/同步/容量 API |
| `usrcode/01_app/Inc/app_sd.h` + `Src/app_sd.c` | 应用层：挂载 FatFs、每 5 s 写诊断快照 CSV、256 KB 自动滚动 |

### 2.2 修改文件

| 文件 | 改动 |
| --- | --- |
| `Core/Inc/stm32f4xx_hal_conf.h` | 打开 `HAL_SD_MODULE_ENABLED` |
| `usrcode/05_common/Inc/module_cfg.h` | 新增 `BSP_SD_ENABLE=1`、`APP_SD_ENABLE=1`（含依赖检查） |
| `usrcode/01_app/Inc/app_watchdog.h` | 新增看门狗槽位 `WDT_TASK_SD=7`，总数 8 |
| `usrcode/01_app/Src/app_tasks.c` | 新建 `SdLogTask`（栈 4 KB、BelowNormal、注册看门狗） |
| `Core/Inc/FreeRTOSConfig.h` | `configTOTAL_HEAP_SIZE` 20480 → 32768 |
| `CMakeLists.txt`（根） | 纳入 FatFs 源码（含 option/unicode.c）与头文件路径 |
| `cmake/stm32cubemx/CMakeLists.txt` | 纳入 `stm32f4xx_hal_sd.c`、`stm32f4xx_ll_sdmmc.c` |

### 2.3 日志格式

文件：`/SMART/LOG0001.CSV`（启动时扫描已有文件，取最大编号新建下一个；`LOG9999` 后回绕）。

表头 + 每 5 s 一行，UTF-8 编码：

```csv
uptime_s,temp_c,hum_rh,vib_g,current_a,voltage_v,power_w,alarm
12345,26.3,58.2,0.012,1.234,24.10,0.080,0
```

| 列 | 来源 |
| --- | --- |
| uptime_s | 系统运行秒数（HAL_GetTick/1000） |
| temp_c / hum_rh | SHT30 温湿度 |
| vib_g | QMI8658 振动加速度 |
| current_a / voltage_v / power_w | INA226 电流/电压/功率 |
| alarm | 告警标志（0 无，1 有） |

- 每条记录 `f_sync` 立即落盘，掉电不丢已写入数据。
- 单文件超过 256 KB 自动滚动到下一个编号。
- 卡拔出/写失败 → 自动关闭文件、置未就绪、每 10 s 重试挂载。
- 其他模块可调用 `APP_SD_AppendLine("...\r\n")` 追加事件行（线程安全）。

## 3. 对外接口

| 接口 | 说明 |
| --- | --- |
| `BSP_SD_Init()` | SDIO 初始化（识别 400 kHz → 4 位 → 24 MHz） |
| `BSP_SD_ReadBlocks / WriteBlocks / Sync / GetCardInfo / IsReady` | 底层块读写与状态 |
| `APP_SD_Task()` | 存储任务入口（FreeRTOS 自动创建） |
| `APP_SD_IsReady()` | 是否已挂载就绪（可接 OLED 显示） |
| `APP_SD_AppendLine()` | 追加一行日志（线程安全） |
| `APP_SD_Format()` | 格式化为 FAT32（危险，需确认用户意图后接入按键/串口命令） |

> `APP_SD_Format` 目前没有暴露触发入口，属安全起见；需要时可在 `app_key`/`app_hex_cmd` 里挂接。

## 4. CubeMX 重新生成工程时注意

- **引脚冲突**：`PC11` 在 `SMART-M.ioc` 中已被配置为 `GPIO_Output`（Label=`trig`，HC-SR04 预留，Locked）。
  - 当前 HC-SR04 已禁用（`BSP_HC_SR04_ENABLE=0` / `APP_HC_SR04_ENABLE=0`），bsp_sd.c 在运行时把 PC11 重配为 AF12，**实际可正常工作**；
  - 但 CubeMX 一旦重新生成代码会覆盖成 GPIO_Output，SD 卡失效。正式做法二选一：
    1. 在 CubeMX 中把 PC8–PC12 配为 `SDIO`（D0–D3、CK），PD2 配为 `SDIO CMD`，并删除 PC11 的 trig 配置；
    2. 或保持 .ioc 不动，每次重新生成后重跑本说明的接线（不推荐）。
- SDIO 时钟源：bsp_sd.c 使用 PLLQ（48 MHz），无需在 CubeMX 里额外配置时钟（驱动内自动使能）。

## 5. 板卡验证步骤

1. 插入一张 FAT32 格式的 TF 卡（建议先格式化，≥2 GB）；
2. 烧录 `build/Debug/SMART-M.bin`（或 .hex），复位；
3. 串口 1（默认调试串口）观察：
   - `[SD] card ok: 15946 MB` —— 初始化成功；
   - `[SD] mounted, log=0:/SMART/LOG0001.CSV` —— 挂载成功；
4. 等 10 s 左右拔卡插到电脑，查看 `SMART/LOG0001.CSV` 是否持续有数据行；
5. 拔卡再插回，设备应在 10 s 内自动重新挂载并续写新文件。

## 5.1 常见问题：串口看不到 [SD] 信息

- **现象**：日志只有 MQTT/ESP8266 内容，没有任何 `[SD]` 打印。
- **根因（已修复）**：`BSP_UART1_Printf()`（bsp_uart.c）内置打印过滤器，默认只放行含 `MQTT` / `WiFi` / `ESP8266` / `BT24` / `ALARM] Temp` 的字符串，**`[SD]` 开头的打印全被丢弃** —— SD 任务其实一直在跑。
  - 次要问题：IWDG 看门狗超时约 3 s，旧代码在"卡未就绪"分支整段延时 10 s 才喂狗，无卡时系统反复复位（已改为每 500 ms 喂狗）。
- **当前行为**（新固件）：
  - 启动即打印 `[SD] task running, free heap=xxxx B`（确认任务创建成功 + 堆余量）；
  - 卡未就绪时每 30 s 打印一条 `[SD] not ready, retrying...`；
  - 任务创建失败打印 `[SD] task create FAIL`；
  - 以上过滤条件已加入 `[SD]`。
- **排查顺序**：
  1. 确认烧录的是最新 `build/Debug/SMART-M.bin`（时间戳应为最近一次编译）；
  2. 清空串口接收区后复位，看启动后 2 s 内是否有 `[SD] task running`；
  3. 无 `[SD] task running` 而有 `[SD] task create FAIL` → FreeRTOS 堆不足，调大 `FreeRTOSConfig.h` 的 `configTOTAL_HEAP_SIZE` 或减小 SD 任务栈（`app_tasks.c` 中 1024\*4 → 1024\*2）；
  4. 有 `[SD] task running` 但无 `card ok` / `mounted` → 检查插卡/接触/格式化，按需加外部上拉（见第 1 节）。

## 6. 已知限制

- 未做掉电/热插拔 100% 健壮性验证（需真机长时间测试）；
- 数据只写不读回校验（日志场景足够）；
- 首次使用未格式化/非 FAT 卡时挂载会失败并每 10 s 重试；可先用 `APP_SD_Format()` 格式化。
