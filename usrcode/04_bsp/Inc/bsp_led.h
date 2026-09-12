/**
 ******************************************************************************
 * @file    bsp_led.h
 * @brief   LED驱动头文件 — 封装LED GPIO操作，与具体引脚解耦
 *
 *  硬件映射（STM32F407ZET6 开发板，低电平点亮）：
 *    LED1 -> PG14
 *    LED2 -> PG13
 *
 *  设计原则：
 *    - BSP层不依赖RTOS，保证可在裸机环境使用
 *    - 仅依赖STM32 HAL库，不直接操作寄存器
 *    - 向上层提供统一API，换硬件只需修改本驱动
 ******************************************************************************
 */
#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdint.h>

/**
 * @brief  LED枚举 — 对应板载LED灯
 */
typedef enum {
    BSP_LED1 = 0,   /* PG14 */
    BSP_LED2 = 1,   /* PG13 */
    BSP_LED3 = 2,   /* PG6 */
    BSP_LED4 = 3    /* PG11 */
} BSP_LED_t;

/**
 * @brief  点亮指定LED
 * @param  led  LED编号
 */
void BSP_LED_On(BSP_LED_t led);

/**
 * @brief  熄灭指定LED
 * @param  led  LED编号
 */
void BSP_LED_Off(BSP_LED_t led);

/**
 * @brief  翻转指定LED状态
 * @param  led  LED编号
 */
void BSP_LED_Toggle(BSP_LED_t led);

/**
 * @brief  获取指定LED当前状态
 * @param  led  LED编号
 * @return 1=亮, 0=灭
 */
uint8_t BSP_LED_GetState(BSP_LED_t led);

#endif /* BSP_LED_H */
