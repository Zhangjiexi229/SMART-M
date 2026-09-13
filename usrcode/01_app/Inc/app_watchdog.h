/**
 ******************************************************************************
 * @file    app_watchdog.h
 * @brief   多任务心跳检测 — 所有关键任务心跳正常才喂狗，任一任务卡死则触发看门狗复位
 ******************************************************************************
 */
#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include "module_cfg.h"

#if APP_WATCHDOG_ENABLE

#include <stdint.h>

/* 受监控的任务 ID（按 SMART-M 实际启用的关键任务定义） */
typedef enum {
    WDT_TASK_MQTT        = 0,   /* MQTT 上云任务 */
    WDT_TASK_SHT30       = 1,   /* SHT30 温湿度采集 */
    WDT_TASK_QMI8658     = 2,   /* QMI8658 六轴IMU采集 */
    WDT_TASK_INA226      = 3,   /* INA226 电源监测 */
    WDT_TASK_ALARM       = 4,   /* 告警联动 */
    WDT_TASK_KEY_MATRIX  = 5,   /* 4x4 矩阵键盘 */
    WDT_TASK_OLED        = 6,   /* OLED 显示 */
    WDT_TASK_COUNT       = 7
} WDT_TaskID_t;

/**
 * @brief  任务心跳：在任务主循环中调用，表明任务仍在正常运行
 * @param  id  任务 ID
 */
void Watchdog_Kick(WDT_TaskID_t id);

/**
 * @brief  注册任务到看门狗监控列表（任务创建成功后调用）
 * @note   未注册的任务不参与心跳检查，避免关闭模块后误判复位
 */
void Watchdog_Register(WDT_TaskID_t id);

/**
 * @brief  带心跳的延时：把长 osDelay 拆成 500ms 小段，每段 Kick 一次
 * @note   用于周期>1s 的任务，避免被 watchdog 误判为卡死
 * @param  id  任务 ID
 * @param  ms  总延时（毫秒）
 */
void Watchdog_DelayWithKick(WDT_TaskID_t id, uint32_t ms);

/**
 * @brief  看门狗监控任务（周期 1s 检查所有任务心跳）
 * @note   由 app_tasks.c 创建为 FreeRTOS 任务（最先创建）
 */
void APP_Watchdog_Task(void *argument);

#endif /* APP_WATCHDOG_ENABLE */
#endif /* APP_WATCHDOG_H */
