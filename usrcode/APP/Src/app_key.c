/**
 ******************************************************************************
 * @file    app_key.c
 * @brief   按键应用任务实现 — 周期扫描 + 消抖事件 + BEEP/LED动作映射
 *
 *  工作流程：
 *    1. 调用 BSP_KEY_Init() 初始化消抖状态
 *    2. 每 APP_KEY_SCAN_PERIOD_MS(10ms) 调用 BSP_KEY_Scan()
 *    3. 用 BSP_KEY_PopEvent() 检测各键"按下事件"（一次性，消抖确认）
 *    4. 按键事件 -> 调用 BSP_BEEP_On/Off 与 BSP_LED_On/Off 执行对应动作
 *
 *  按键映射：
 *    KEY0(PG2) -> BEEP开 + LED1亮    KEY1(PG3) -> BEEP关 + LED1灭
 *    KEY2(PG4) -> LED2开              KEY3(PG5) -> LED2灭
 *
 *  设计原则：
 *    - APP层协调BSP模块（按键输入 + BEEP/LED输出），不直接操作寄存器
 *    - 消抖在BSP层完成，本层只消费"已确认的按下事件"
 *    - 用PopEvent一次性事件，避免长按重复触发
 *    - BEEP相关调用受 BSP_BEEP_ENABLE 条件编译保护
 *    - LED相关调用受 BSP_LED_ENABLE && APP_KEY_LED_ENABLE 条件编译保护
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_KEY_ENABLE

#include "app_key.h"
#include "bsp_key.h"
#if BSP_LED_ENABLE
#include "bsp_led.h"
#endif

#if BSP_BEEP_ENABLE
#include "bsp_beep.h"
#endif
#include "cmsis_os.h"

/**
 * @brief 按键扫描周期(ms) — 10ms扫描 × BSP_KEY_DEBOUNCE_CNT(2) = 20ms消抖
 */
#define APP_KEY_SCAN_PERIOD_MS   10U

/**
 * @brief  按键扫描任务 — FreeRTOS任务入口
 * @param  argument  未使用
 */
void APP_KEY_Task(void *argument)
{
    (void)argument;

    BSP_KEY_Init();   /* 初始化消抖状态与事件标志 */

    for (;;) {
        BSP_KEY_Scan();   /* 周期扫描：读取原始电平 + 消抖状态机 + 事件置位 */

        /* 检测各键按下事件（PopEvent读取后清零，一次性触发） */
        if (BSP_KEY_PopEvent(BSP_KEY0) != 0U) {
#if BSP_BEEP_ENABLE
            BSP_BEEP_On();     /* KEY0 -> BEEP开 */
#endif
#if BSP_LED_ENABLE && APP_KEY_LED_ENABLE
            BSP_LED_On(BSP_LED1);    /* LED1同步亮，作为蜂鸣器状态视觉指示 */
#endif
        }
        if (BSP_KEY_PopEvent(BSP_KEY1) != 0U) {
#if BSP_BEEP_ENABLE
            BSP_BEEP_Off();    /* KEY1 -> BEEP关 */
#endif
#if BSP_LED_ENABLE && APP_KEY_LED_ENABLE
            BSP_LED_Off(BSP_LED1);   /* LED1同步灭 */
#endif
        }
        if (BSP_KEY_PopEvent(BSP_KEY2) != 0U) {
#if BSP_LED_ENABLE && APP_KEY_LED_ENABLE
            BSP_LED_On(BSP_LED2);    /* KEY2 -> LED2开 */
#endif
        }
        if (BSP_KEY_PopEvent(BSP_KEY3) != 0U) {
#if BSP_LED_ENABLE && APP_KEY_LED_ENABLE
            BSP_LED_Off(BSP_LED2);   /* KEY3 -> LED2关 */
#endif
        }

        osDelay(APP_KEY_SCAN_PERIOD_MS);
    }
}

#endif /* APP_KEY_ENABLE */
