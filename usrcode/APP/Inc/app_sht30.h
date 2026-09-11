/**
 ******************************************************************************
 * @file    app_sht30.h
 * @brief   SHT30温湿度应用任务头文件 — 周期采集+串口打印+共享快照
 *
 *  任务流程：
 *    1. BSP_SHT30_Read() 读取温湿度（失败重试3次）
 *    2. 更新统一快照 APP_SENSOR_UpdateSHT30()（供MQTT上报）
 *    3. BSP_UART1_Printf() 打印温湿度
 *    4. osDelay(2000ms) 阻塞释放CPU
 *
 *  采集周期：2000ms（SHT30 单次测量约20ms，2秒周期留足余量）
 *
 *  共享快照：SHT30 任务每次采集后更新 app_sensor 统一快照，
 *  MQTT 任务通过 APP_SENSOR_GetSnapshot() 读取（SHT30 优先级高于 DHT11）。
 ******************************************************************************
 */
#ifndef APP_SHT30_H
#define APP_SHT30_H

#include <stdint.h>

/* ========== 任务参数 ========== */
#define SHT30_PERIOD_MS       2000U   /*!< 采集周期(ms) */
#define SHT30_RETRY_COUNT     3U      /*!< 读取失败重试次数 */

/**
 * @brief  SHT30温湿度采集任务入口
 * @param  argument 任务参数（未使用）
 * @note   由 app_tasks.c 创建为 FreeRTOS 任务
 */
void APP_SHT30_Task(void *argument);

#endif /* APP_SHT30_H */
