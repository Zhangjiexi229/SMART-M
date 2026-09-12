/**
 ******************************************************************************
 * @file    app_light_sensor.h
 * @brief   光敏传感器应用任务头文件 — 周期采集 + 串口打印 + LED分级控制
 *
 *  阈值逻辑（光敏电阻接GND，光照越强ADC值越小）：
 *    ADC >= 500  → 暗光(DARK)   → LED1亮, LED2亮
 *    200~500     → 中等光(MEDIUM)→ LED1灭, LED2亮
 *    ADC < 200   → 强光(BRIGHT) → LED1灭, LED2灭
 *
 *  共享快照：光敏任务每次采集后更新 s_snapshot，WiFi任务通过
 *  APP_LightSensor_GetSnapshot() 读取最新光照数据用于上报。
 *  快照接口始终可用，即使 APP_LIGHT_SENSOR_ENABLE=0 也返回 valid=0 的空快照。
 *
 *  阈值微调方法：
 *    - 手挡住时LED还没亮 → 把 LIGHT_TH_HIGH 调大（如600、700）
 *    - 灯光照射时LED还没灭 → 把 LIGHT_TH_LOW 调大（如300、400）
 *    - 两个阈值间隔建议 >= 200，避免临界值附近LED频繁闪烁
 ******************************************************************************
 */
#ifndef APP_LIGHT_SENSOR_H
#define APP_LIGHT_SENSOR_H

#include <stdint.h>

/* ========== 阈值宏定义（可按实际环境调整） ========== */
#define LIGHT_TH_LOW            200U    /*!< 低于此值=强光，LED全灭 */
#define LIGHT_TH_HIGH           500U    /*!< 高于此值=暗光，LED全亮 */
/* 200 <= ADC < 500 为中等光区间 */

/* ========== 任务参数 ========== */
#define LIGHT_SENSOR_PERIOD_MS  1000U    /*!< 采集周期(ms)，每500ms采集打印一次 */

/* ========== 光照等级 ========== */
#define LIGHT_LEVEL_BRIGHT      0U      /*!< 强光 */
#define LIGHT_LEVEL_MEDIUM      1U      /*!< 中等光 */
#define LIGHT_LEVEL_DARK        2U      /*!< 暗光 */

/* ========== 光敏传感器共享快照（光敏任务写，WiFi任务读） ========== */

/** @brief 光敏传感器快照 */
typedef struct {
    uint16_t adc_value;       /*!< ADC 原始值（12位，0~4095） */
    uint16_t voltage_mv;      /*!< 换算电压（毫伏） */
    uint8_t  level;           /*!< 光照等级：LIGHT_LEVEL_BRIGHT/MEDIUM/DARK */
    uint8_t  valid;           /*!< 数据有效标志：1=有效，0=尚未采集 */
} LightSensor_Snapshot_t;

/**
 * @brief  光敏传感器任务入口
 * @param  argument 任务参数（未使用）
 * @note   周期采集ADC → 判定光照等级 → 控制LED → 串口打印
 */
void APP_LightSensor_Task(void *argument);

/**
 * @brief  获取最新光敏传感器快照（线程安全，内部临界区保护）
 * @param  out 输出快照结构体指针
 * @note   即使光敏任务未启用，也会返回 valid=0 的空快照
 */
void APP_LightSensor_GetSnapshot(LightSensor_Snapshot_t *out);

#endif /* APP_LIGHT_SENSOR_H */
