/**
 ******************************************************************************
 * @file    app_dht11.h
 * @brief   DHT11温湿度传感器应用任务头文件 — 周期采集+串口打印+温湿度联动LED
 *
 *  阈值逻辑（宏定义，可按实际需求调整）：
 *
 *  温度控制（LED1=PG14, LED2=PG13）：
 *    温度 >= 28℃  → LED1亮, LED2亮（高温提醒）
 *    温度 <  28℃  → LED1灭, LED2亮（正常）
 *
 *  湿度控制（LED3=PG6, LED4=PG11）：
 *    湿度 >= 60%   → LED3亮, LED4亮（高湿提醒）
 *    湿度 <  60%   → LED3灭, LED4亮（正常，含28%~60%区间）
 *
 *  采集周期：2000ms（DHT11最快1Hz，建议>=2秒）
 *
 *  共享快照：DHT11任务每次采集成功后更新 s_snapshot，WiFi任务通过
 *  APP_DHT11_GetSnapshot() 读取最新温湿度用于上报。读写均在临界区保护。
 *  快照接口始终可用，即使 APP_DHT11_ENABLE=0 也返回 valid=0 的空快照。
 *
 *  阈值微调方法：
 *    - 觉得高温阈值太高/太低 → 调整 TEMP_THRESHOLD
 *    - 觉得高湿阈值太高/太低 → 调整 HUMIDITY_TH_HIGH
 ******************************************************************************
 */
#ifndef APP_DHT11_H
#define APP_DHT11_H

#include <stdint.h>

/* ========== 阈值宏定义（可按实际需求调整） ========== */
#define TEMP_THRESHOLD        30U    /* 温度阈值(℃)，>=此值LED1亮 */
#define HUMIDITY_TH_HIGH      60U    /* 湿度高阈值(%)，>=此值LED3亮 */
#define HUMIDITY_TH_LOW       30U    /* 湿度低阈值(%)，滞回下限（参考值） */

/* ========== 任务参数 ========== */
#define DHT11_PERIOD_MS       5000U  /* 采集周期(ms)，DHT11建议>=2秒 */
#define DHT11_RETRY_COUNT     3U     /* 读取失败重试次数 */

/* ========== 温湿度共享快照（DHT11任务写，WiFi任务读） ========== */

/** @brief DHT11 温湿度快照 */
typedef struct {
    uint8_t  temperature_int;   /*!< 温度整数部分（摄氏度） */
    uint8_t  temperature_dec;   /*!< 温度小数部分 */
    uint8_t  humidity_int;      /*!< 湿度整数部分（%RH） */
    uint8_t  humidity_dec;      /*!< 湿度小数部分 */
    uint8_t  valid;             /*!< 数据有效标志：1=有效，0=尚未采集到有效数据 */
} DHT11_Snapshot_t;

/**
 * @brief  DHT11温湿度采集任务入口
 * @param  argument 任务参数（未使用）
 * @note   周期采集温湿度 → 判定阈值 → 控制LED → 串口打印
 */
void APP_DHT11_Task(void *argument);

/**
 * @brief  获取最新温湿度快照（线程安全，内部临界区保护）
 * @param  out 输出快照结构体指针
 * @note   即使 DHT11 任务未启用，也会返回 valid=0 的空快照
 */
void APP_DHT11_GetSnapshot(DHT11_Snapshot_t *out);

#endif /* APP_DHT11_H */
