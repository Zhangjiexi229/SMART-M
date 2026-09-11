/**
 ******************************************************************************
 * @file    app_ina226.h
 * @brief   INA226电源监测应用任务头文件 — 周期采集+过流保护+共享快照
 *
 *  任务流程：
 *    1. BSP_INA226_Read() 读取母线电压/电流/功率（失败重试3次）
 *    2. 更新统一快照 APP_SENSOR_UpdatePower()（供MQTT上报）
 *    3. 过流保护：电流 > 阈值 → 断开继电器（电机断电）+ 蜂鸣器报警
 *    4. 每 PRINT_DIV 次采集串口打印一次
 *    5. osDelay(500ms) 阻塞释放CPU
 *
 *  采集周期：500ms（电源监测无需过快；过流保护响应时间 <1s）
 *
 *  过流保护逻辑：
 *    - 电流 >= INA226_OVERCURRENT_MA 且继电器当前吸合 → 断开继电器
 *    - 触发后打印告警；电流回落到阈值以下可重新吸合（保护不锁定）
 ******************************************************************************
 */
#ifndef APP_INA226_H
#define APP_INA226_H

#include <stdint.h>

/* ========== 任务参数 ========== */
#define INA226_PERIOD_MS       500U    /*!< 采集周期(ms) */
#define INA226_RETRY_COUNT     3U      /*!< 读取失败重试次数 */
#define INA226_PRINT_DIV       2U      /*!< 每N次采集打印一次（2次=1秒） */

/* ========== 过流保护参数 ========== */
#define INA226_OVERCURRENT_MA  3000    /*!< 过流阈值(mA)=3A，按电机额定电流调整 */

/**
 * @brief  INA226电源监测任务入口
 * @param  argument 任务参数（未使用）
 * @note   由 app_tasks.c 创建为 FreeRTOS 任务
 */
void APP_INA226_Task(void *argument);

#endif /* APP_INA226_H */
