/**
 ******************************************************************************
 * @file    bsp_delay.h
 * @brief   板级微秒延时驱动头文件 — 自由计数器方案（TIM2 / DWT 可选）
 *
 *  ▍资源分配（不冲突 / 不抢占 / 不相互影响）：
 *    SysTick -> FreeRTOS（tick 调度，独占，本模块不碰）
 *    TIM6    -> HAL 时基（HAL_GetTick / HAL_Delay，独占）
 *    TIM2    -> 用户微秒延时（I2C + DHT11 共享，只读 CNT）
 *
 *  ▍为什么用 TIM2 而不是 DWT->CYCCNT：
 *    DWT 属于内核调试/追踪块，在"调试器下载后"场景下可能因调试块未被
 *    干净复位而不计数，导致 BSP_DelayUs 忙等死循环（现象：烧录后 DHT11
 *    不打印，按物理 RESET 才恢复）。TIM2 是普通外设，由 RCC 定时器时钟
 *    驱动，任何复位后都保证计数，作为 µs 延时源最稳妥。
 *
 *  ▍共享安全性：
 *    TIM2 做成"自由计数器"（只读 CNT），DHT11 与 OLED 软件 I2C 都只是
 *    读同一个只增计数器算耗时，读操作不改变任何状态，即使被任务抢占，
 *    忙等只会"顺延"，结果仍然正确，互不干扰。
 *
 *  调用方式：
 *    BSP_Delay_Init()  在任务调度器启动前调用一次（APP_Init 内）
 *    BSP_DelayUs(us)   任意上下文（任务/中断外）调用
 ******************************************************************************
 */
#ifndef BSP_DELAY_H
#define BSP_DELAY_H

#include <stdint.h>

/* ========== 延时源选择 ========== */
/* 1=TIM2 32位自由计数器（推荐，最稳妥，不依赖调试块）
 * 0=DWT->CYCCNT 周期计数器（不占外设，但调试器下载场景下有隐患） */
#define BSP_DELAY_USE_TIM2  1

/**
 * @brief  初始化微秒延时源
 * @note   在任务调度器启动前调用一次（APP_Init）
 */
void BSP_Delay_Init(void);

/**
 * @brief  微秒级阻塞延时
 * @param  us  延时微秒数
 * @note   忙等方式；短延时（微秒级）使用，长延时请用 vTaskDelay
 */
void BSP_DelayUs(uint32_t us);

/**
 * @brief  读取当前微秒计数值（1 tick = 1us）
 * @return 当前 µs 计数值（32位，自动回绕）
 * @note   用于"测量耗时"场景：
 *         uint32_t start = BSP_Delay_GetTickUs();
 *         ...被测代码...
 *         uint32_t elapsed_us = BSP_Delay_GetTickUs() - start;   // 32位减法自动处理回绕
 */
uint32_t BSP_Delay_GetTickUs(void);

/**
 * @brief  毫秒级阻塞延时（与 BSP_DelayUs 同一时间源）
 * @param  ms  延时毫秒数
 * @note   协议时序一致性最好；仅用于短时协议时序（如 DHT11 起始信号）。
 *         FreeRTOS 任务内做"周期等待"请用 osDelay/vTaskDelay 让出 CPU
 */
void BSP_DelayMs(uint32_t ms);

#endif /* BSP_DELAY_H */
