/**
 ******************************************************************************
 * @file    bsp_key.h
 * @brief   按键驱动头文件 — 封装按键GPIO读取与消抖，与具体引脚解耦
 *
 *  硬件映射（STM32F407ZET6 开发板，外部上拉，按下为低电平）：
 *    KEY0 -> PG2
 *    KEY1 -> PG3
 *    KEY2 -> PG4
 *    KEY3 -> PG5
 *
 *  消抖方案：
 *    - 周期扫描（建议10ms调用一次 BSP_KEY_Scan()）
 *    - 连续 BSP_KEY_DEBOUNCE_CNT 次采样一致才确认状态变更
 *    - 检测"释放->按下"跃迁，置位 press_evt 事件标志
 *    - BSP_KEY_PopEvent() 读取并清除事件标志（一次性事件）
 *
 *  设计原则：
 *    - BSP层不依赖RTOS，保证可在裸机环境使用
 *    - 仅依赖STM32 HAL库，不直接操作寄存器
 *    - 向上层提供统一API，换硬件只需修改本驱动
 ******************************************************************************
 */
#ifndef BSP_KEY_H
#define BSP_KEY_H

#include <stdint.h>

/**
 * @brief  按键枚举 — 对应板载按键
 */
typedef enum {
    BSP_KEY0 = 0,   /* PG2 */
    BSP_KEY1 = 1,   /* PG3 */
    BSP_KEY2 = 2,   /* PG4 */
    BSP_KEY3 = 3    /* PG5 */
} BSP_KEY_t;

/**
 * @brief  按键状态
 */
typedef enum {
    BSP_KEY_RELEASED = 0,   /* 释放（高电平） */
    BSP_KEY_PRESSED  = 1    /* 按下（低电平） */
} BSP_KEY_State_t;

/**
 * @brief  按键数量
 */
#define BSP_KEY_COUNT   4U

/**
 * @brief  初始化按键驱动（清零消抖状态与事件标志）
 * @note   GPIO配置由CubeMX MX_GPIO_Init()完成，本函数仅初始化软件状态
 */
void BSP_KEY_Init(void);

/**
 * @brief  读取按键原始电平（未消抖）
 * @param  key  按键编号
 * @return BSP_KEY_PRESSED=按下(低电平), BSP_KEY_RELEASED=释放(高电平)
 */
BSP_KEY_State_t BSP_KEY_ReadRaw(BSP_KEY_t key);

/**
 * @brief  按键扫描 — 周期调用，内部完成消抖与事件检测
 * @note   建议每10ms调用一次；消抖阈值由 BSP_KEY_DEBOUNCE_CNT 决定
 */
void BSP_KEY_Scan(void);

/**
 * @brief  获取按键消抖后的稳定状态
 * @param  key  按键编号
 * @return BSP_KEY_PRESSED / BSP_KEY_RELEASED
 */
BSP_KEY_State_t BSP_KEY_GetState(BSP_KEY_t key);

/**
 * @brief  弹出按键按下事件（读取并清除事件标志）
 * @param  key  按键编号
 * @return 1=自上次调用后发生过一次"释放->按下"跃迁；0=无事件
 * @note   一次性事件：调用后标志清零，适合"按一次执行一次"的场景
 */
uint8_t BSP_KEY_PopEvent(BSP_KEY_t key);

#endif /* BSP_KEY_H */
