/**
 ******************************************************************************
 * @file    bsp_hc_sr04.h
 * @brief   HC-SR04 超声波测距模块驱动头文件
 *
 *  硬件连接：
 *    Trig -> PC11 (推挽输出，主机触发)
 *    Echo -> PE5  (浮空/上拉输入，从机回波)
 *
 *  测量原理：
 *    1. Trig 输入 ≥10us 高电平触发测距
 *    2. Echo 输出高电平，持续时间 = 声波往返时间
 *    3. 距离(mm) = Echo高电平脉宽(us) × 0.17 (声速340m/s ÷ 2往返)
 *    4. 测量范围 20mm ~ 4000mm
 *
 *  时序参数：
 *    触发脉冲 ≥10us
 *    测量周期 ≥60ms（避免回波干扰）
 *    超时 30ms（对应最大距离约5100mm，留余量）
 ******************************************************************************
 */
#ifndef BSP_HC_SR04_H
#define BSP_HC_SR04_H

#include <stdint.h>

/* ========== 测量参数 ========== */
#define HC_SR04_TRIG_US         12U     /* 触发脉冲宽度(us)，≥10us */
#define HC_SR04_TIMEOUT_US      30000U  /* Echo超时(us)，30ms */
#define HC_SR04_MIN_DIST_MM     20U     /* 最小有效距离(mm) */
#define HC_SR04_MAX_DIST_MM     4000U   /* 最大有效距离(mm) */

/* ========== API ========== */

/**
 * @brief  初始化 HC-SR04 GPIO（Trig推挽输出, Echo输入）
 * @note   使能GPIOC/GPIOE时钟，配置引脚模式
 */
void BSP_HC_SR04_Init(void);

/**
 * @brief  触发一次测距并返回距离
 * @param  distance_mm  输出：距离(毫米)
 * @retval 0=成功, 1=超时(无回波/超出范围), 2=参数错误
 * @note   阻塞式测量，最长约30ms；在FreeRTOS任务中调用可接受
 */
uint8_t BSP_HC_SR04_MeasureMm(uint16_t *distance_mm);

#endif /* BSP_HC_SR04_H */
