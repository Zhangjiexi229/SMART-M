/**
 ******************************************************************************
 * @file    app_key_matrix.h
 * @brief   4x4矩阵键盘应用层头文件 — 消抖 + 按键功能映射
 *
 *  对应工作任务 Day3「4x4矩阵按键」与 Day5「按键设置阈值（完整交互）」：
 *
 *  正常模式（SET_MODE_EXIT）：
 *    A  切换OLED页面（实时/阈值/状态 循环）
 *    B  手动控制继电器（吸合/断开切换）
 *    C  蜂鸣器测试（短鸣100ms）
 *    *  进入阈值设置模式（SET_MODE_SELECT，跳转阈值页）
 *
 *  选择模式（SET_MODE_SELECT）：
 *    2/8  上/下切换阈值项（温度/振动/电流）
 *    A    确认进入调整（SET_MODE_ADJUST）
 *    *    退出设置模式
 *
 *  调整模式（SET_MODE_ADJUST）：
 *    2/#  数值加（温度+1℃，振动/电流+0.1）
 *    8 与 * 键  数值减（下限保护）
 *    A    确认保存（写入内部Flash）+ 返回选择
 *    B    取消返回选择
 *
 *  消抖：连续3次扫描相同键值才生效（50ms周期×3 ≈ 150ms）
 ******************************************************************************
 */
#ifndef APP_KEY_MATRIX_H
#define APP_KEY_MATRIX_H

/**
 * @brief  矩阵键盘扫描任务（50ms周期：扫描+消抖+按键动作）
 * @param  argument 未使用（FreeRTOS入口）
 */
void APP_KeyMatrix_Task(void *argument);

#endif /* APP_KEY_MATRIX_H */
