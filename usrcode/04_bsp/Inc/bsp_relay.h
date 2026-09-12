/**
 ******************************************************************************
 * @file    bsp_relay.h
 * @brief   继电器驱动头文件 — GPIO控制（PA4，高电平吸合）
 *
 *  硬件连接：
 *    RELAY IN -> PA4（推挽输出，GPIO_SPEED_FREQ_LOW）
 *    VCC/GND  -> 5V / GND（模块自带光耦与续流二极管）
 *    COM/NO   -> 串接在被控电路（如电机电源回路），吸合=接通
 *
 *  逻辑约定（本驱动默认）：
 *    PA4 = SET   → 继电器吸合（ON），负载通电
 *    PA4 = RESET→ 继电器断开（OFF），负载断电
 *
 *  若使用"低电平触发"继电器模块，将 BSP_RELAY_ACTIVE_HIGH 改为 0 即可。
 ******************************************************************************
 */
#ifndef BSP_RELAY_H
#define BSP_RELAY_H

#include <stdint.h>

/* ========== 引脚映射 ========== */
#define RELAY_GPIO_Port         GPIOA
#define RELAY_Pin               GPIO_PIN_4

/* ========== 触发极性 ========== */
#define BSP_RELAY_ACTIVE_HIGH   1U   /*!< 1=高电平吸合, 0=低电平吸合 */

/**
 * @brief  继电器 GPIO 初始化（PA4推挽输出，初始断开）
 * @note   在任务调度器启动前调用（APP_Init）
 */
void BSP_RELAY_Init(void);

/**
 * @brief  继电器吸合（ON）
 */
void BSP_RELAY_On(void);

/**
 * @brief  继电器断开（OFF）
 */
void BSP_RELAY_Off(void);

/**
 * @brief  翻转继电器状态
 */
void BSP_RELAY_Toggle(void);

/**
 * @brief  获取继电器当前状态
 * @retval 1=吸合(ON), 0=断开(OFF)
 */
uint8_t BSP_RELAY_GetState(void);

#endif /* BSP_RELAY_H */
