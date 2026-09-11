/**
 ******************************************************************************
 * @file    bsp_beep.h
 * @brief   有源蜂鸣器驱动头文件 — 封装BEEP GPIO操作，与具体引脚解耦
 *
 *  硬件映射（STM32F407ZET6 开发板，NPN三极管驱动）：
 *    BEEP -> PA5
 *
 *  驱动电路：
 *    PA5 --R12-- Q1(NPN)基极
 *    Q1集电极 -- 蜂鸣器 -- VCC5V
 *    Q1发射极 -- GND
 *    R11为基极下拉电阻（确保PA5低电平时Q1可靠截止）
 *
 *  电平逻辑：
 *    PA5输出高电平(SET)   -> Q1导通 -> 蜂鸣器响
 *    PA5输出低电平(RESET) -> Q1截止 -> 蜂鸣器不响
 *
 *  设计原则：
 *    - BSP层不依赖RTOS，保证可在裸机环境使用
 *    - 仅依赖STM32 HAL库，不直接操作寄存器
 *    - 向上层提供统一API，换硬件只需修改本驱动
 ******************************************************************************
 */
#ifndef BSP_BEEP_H
#define BSP_BEEP_H

#include <stdint.h>

/**
 * @brief  蜂鸣器打开（输出高电平，Q1导通，蜂鸣器响）
 */
void BSP_BEEP_On(void);

/**
 * @brief  蜂鸣器关闭（输出低电平，Q1截止，蜂鸣器不响）
 */
void BSP_BEEP_Off(void);

/**
 * @brief  翻转蜂鸣器状态
 */
void BSP_BEEP_Toggle(void);

/**
 * @brief  获取蜂鸣器当前状态
 * @return 1=响(高电平), 0=不响(低电平)
 */
uint8_t BSP_BEEP_GetState(void);

/**
 * @brief  蜂鸣器短鸣（响指定时长后关闭），阻塞式
 * @param  ms  响的时长(毫秒)
 * @note   阻塞调用，使用HAL_Delay，适合上电自检等非实时场景
 */
void BSP_BEEP_Beep(uint32_t ms);

#endif /* BSP_BEEP_H */
