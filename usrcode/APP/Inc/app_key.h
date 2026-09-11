/**
 ******************************************************************************
 * @file    app_key.h
 * @brief   按键应用任务头文件 — 周期扫描按键，消抖后映射到BEEP/LED动作
 *
 *  功能映射：
 *    KEY0 -> BEEP 开（同时 LED1 亮，作为蜂鸣器状态视觉指示）
 *    KEY1 -> BEEP 关（同时 LED1 灭）
 *    KEY2 -> LED2 开
 *    KEY3 -> LED2 关

 *
 *  集成点：
 *    APP_KEY_Task() -> app_tasks.c 中由 osThreadNew 创建
 *    任务创建受 APP_KEY_ENABLE 条件编译控制
  *    BEEP 动作受 BSP_BEEP_ENABLE 条件编译控制
 ******************************************************************************
 */
#ifndef APP_KEY_H
#define APP_KEY_H

/**
 * @brief  按键扫描任务入口 — FreeRTOS任务函数
 * @param  argument  未使用（保持osThreadFunc_t签名）
 * @note   内部周期调用 BSP_KEY_Scan()，检测到按下事件后执行对应BEEP/LED动作
 */
void APP_KEY_Task(void *argument);

#endif /* APP_KEY_H */
