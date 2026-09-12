/**
 ******************************************************************************
 * @file    app_filter.h
 * @brief   滑动平均滤波器头文件 — 传感器数据去抖
 *
 *  对应工作任务 Day5「数据滤波优化」：
 *    - 滑动窗口平均，窗口越大越平滑、延迟越大
 *    - 温度变化慢用 10~20 点窗口；振动变化快建议 5 点；电流 10 点
 *    - 本工程统一 10 点窗口，10ms 采集周期下相当于 100ms 平均延迟，
 *      对告警响应影响可接受
 ******************************************************************************
 */
#ifndef APP_FILTER_H
#define APP_FILTER_H

#include <stdint.h>

#define APP_FILTER_WINDOW_SIZE  10U   /* 滑动窗口点数 */

typedef struct {
    float    buffer[APP_FILTER_WINDOW_SIZE];
    uint8_t  index;    /* 写入位置 */
    uint8_t  count;    /* 已缓存点数（<窗口时=当前点数，满后=窗口） */
    float    sum;      /* 窗口内累加和 */
} MovingAvgFilter_t;

/**
 * @brief  滤波器初始化（清零缓冲）
 * @param  f  滤波器指针
 */
void APP_FILTER_Init(MovingAvgFilter_t *f);

/**
 * @brief  输入新值，返回当前窗口平均值
 * @param  f          滤波器指针
 * @param  new_value  新采样值
 * @retval 窗口平均（数据不足时按已有点数平均）
 */
float APP_FILTER_Update(MovingAvgFilter_t *f, float new_value);

/**
 * @brief  获取当前平均值（不更新窗口）
 * @param  f  滤波器指针
 * @retval 当前平均值；无数据返回 0
 */
float APP_FILTER_Get(const MovingAvgFilter_t *f);

#endif /* APP_FILTER_H */
