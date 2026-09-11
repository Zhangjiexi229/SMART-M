/**
 ******************************************************************************
 * @file    app_qmi8658.h
 * @brief   QMI8658六轴IMU应用任务头文件 — 周期采集+姿态解算+共享快照
 *
 *  任务流程：
 *    1. BSP_QMI8658_Read() 读取六轴原始数据（失败重试3次）
 *    2. 换算 mg/mdps 工程值，由加速度计算 pitch/roll 姿态角
 *    3. 更新统一快照 APP_SENSOR_UpdateIMU()（供MQTT上报）
 *    4. 每 PRINT_DIV 次采集串口打印一次（避免刷屏）
 *    5. osDelay(100ms) 阻塞释放CPU
 *
 *  采集周期：100ms（电机振动监测需要较高采样率）
 *
 *  姿态角计算（静止/缓变工况有效）：
 *    pitch = atan2(-ax, sqrt(ay^2+az^2)) * 180/PI
 *    roll  = atan2(ay, az) * 180/PI
 *  振动监测用原始加速度幅值 |A| = sqrt(ax^2+ay^2+az^2)（mg）
 ******************************************************************************
 */
#ifndef APP_QMI8658_H
#define APP_QMI8658_H

#include <stdint.h>

/* ========== 任务参数 ========== */
#define QMI8658_PERIOD_MS      10U     /*!< 采集周期(ms)，10ms=100Hz；10ms/点填满100点振动窗口=1秒 */
#define QMI8658_RETRY_COUNT    3U      /*!< 读取失败重试次数 */
#define QMI8658_PRINT_DIV      10U     /*!< 每N次采集打印一次（10次=1秒） */

/**
 * @brief  QMI8658六轴IMU采集任务入口
 * @param  argument 任务参数（未使用）
 * @note   由 app_tasks.c 创建为 FreeRTOS 任务
 */
void APP_QMI8658_Task(void *argument);

#endif /* APP_QMI8658_H */
