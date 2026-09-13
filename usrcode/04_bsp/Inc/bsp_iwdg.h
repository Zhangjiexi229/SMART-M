/**
 ******************************************************************************
 * @file    bsp_iwdg.h
 * @brief   独立看门狗 IWDG 驱动 — 系统跑飞后自动复位
 ******************************************************************************
 */
#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#include "module_cfg.h"

#if BSP_IWDG_ENABLE

#include <stdint.h>

/**
 * @brief  初始化独立看门狗
 * @param  timeout_ms  超时时间（毫秒），建议 2000~4000ms
 * @note   IWDG 用 LSI (~32kHz) 作为时钟源，预分频 64，
 *         超时 = (pre * reload) / 32000 秒
 */
void BSP_IWDG_Init(uint32_t timeout_ms);

/**
 * @brief  喂狗（重置看门狗计数器）
 * @note   必须在超时前调用，否则系统复位
 */
void BSP_IWDG_Refresh(void);

#endif /* BSP_IWDG_ENABLE */
#endif /* BSP_IWDG_H */
