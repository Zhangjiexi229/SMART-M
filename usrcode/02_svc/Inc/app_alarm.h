/**
 ******************************************************************************
 * @file    app_alarm.h
 * @brief   告警管理器头文件 — 三路独立阈值 + 迟滞恢复 + 联动动作
 *
 *  对应工作任务 Day5「阈值告警逻辑完善」「告警联动完整实现」：
 *    - 三路独立阈值：温度(℃)/振动(g)/电流(A)，阈值可按键设置并存入Flash
 *    - 告警等级：ALARM_WARNING（超过阈值80%）/ ALARM_CRITICAL（超过阈值）
 *    - 迟滞恢复：所有指标低于阈值×(1-10%) 才恢复，避免阈值附近频繁抖动
 *    - 联动（由 APP_ALARM_Task 执行）：
 *        触发瞬间 -> 继电器断开（电机断电保护）+ 蜂鸣器急促告警 + OLED闪烁
 *        恢复瞬间 -> 继电器重新合闸 + 蜂鸣器停止 + OLED恢复
 *
 *  告警源位图：
 *    ALARM_SRC_TEMP(bit0) / ALARM_SRC_VIB(bit1) / ALARM_SRC_CURR(bit2)
 ******************************************************************************
 */
#ifndef APP_ALARM_H
#define APP_ALARM_H

#include <stdint.h>
#include "app_diag.h"

/* ========== 告警等级 ========== */
#define ALARM_NONE     0   /* 无告警 */
#define ALARM_WARNING  1   /* 注意：超过阈值的80% */
#define ALARM_CRITICAL 2   /* 异常：超过阈值 */

/* ========== 告警源位图 ========== */
#define ALARM_SRC_TEMP (1U << 0)
#define ALARM_SRC_VIB  (1U << 1)
#define ALARM_SRC_CURR (1U << 2)

/* 告警状态 */
typedef struct {
    uint8_t  active;       /*!< 是否有告警 */
    uint8_t  level;        /*!< 当前最高告警等级 */
    uint8_t  source_mask;  /*!< 告警源位图 */
    uint32_t start_time;   /*!< 告警开始时间（tick） */
    uint32_t duration;     /*!< 持续时间（ms） */
} AlarmStatus_t;

/**
 * @brief  告警管理器初始化（清状态）
 */
void APP_ALARM_Init(void);

/**
 * @brief  更新告警状态机（阈值比较 + 迟滞恢复）
 * @param  data  诊断快照（温度/振动/电流）
 * @note   触发/恢复只改变状态与 g_alarm_active；联动动作在任务中完成
 */
void APP_ALARM_Update(const APP_Diag_Snapshot_t *data);

/**
 * @brief  查询是否处于告警状态
 * @retval 1=告警中，0=正常
 */
uint8_t APP_ALARM_IsActive(void);

/**
 * @brief  获取告警状态详情
 * @param  status  输出告警状态
 */
void APP_ALARM_GetStatus(AlarmStatus_t *status);

/**
 * @brief  告警任务 — 周期100ms：读快照->状态机->联动动作->每秒故障诊断
 * @param  argument 未使用（FreeRTOS入口）
 */
void APP_ALARM_Task(void *argument);

#endif /* APP_ALARM_H */
