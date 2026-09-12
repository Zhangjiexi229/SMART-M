/**
 ******************************************************************************
 * @file    app_hc_sr04.h
 * @brief   HC-SR04 超声波测距应用层头文件 — 周期采集 + 快照 + 可选LED控制
 *
 *  功能：
 *    - FreeRTOS 任务周期测量距离
 *    - 快照机制：其他模块通过 GetSnapshot 获取最新数据
 *    - 可选：距离小于阈值时点亮 LED1（障碍物报警）
 *    - 测量周期/阈值/使能从 EEPROM 配置加载
 *
 *  集成：
 *    - app_tasks.c: APP_Init 中调用 BSP_HC_SR04_Init，创建 UltrasonicTask
 *    - app_hex_cmd.c: 寄存器 0x0024 读距离, 0x0250~0x0252 读写配置
 *    - app_wifi.c: 上报字符串追加 | Dist=xxxmm
 *    - app_cmd.c: 文本指令 DIST? 查询距离
 ******************************************************************************
 */
#ifndef APP_HC_SR04_H
#define APP_HC_SR04_H

#include <stdint.h>

/* ========== 快照结构体 ========== */
typedef struct {
    uint16_t distance_mm;   /* 最新距离(毫米)，无效时为0 */
    uint8_t  valid;         /* 1=数据有效, 0=未测量或超时 */
    uint8_t  measure_count; /* 测量成功次数（溢出回绕） */
} HC_SR04_Snapshot_t;

/* ========== API ========== */

/**
 * @brief  获取最新测量快照
 * @param  snap  输出快照
 */
void APP_HC_SR04_GetSnapshot(HC_SR04_Snapshot_t *snap);

/**
 * @brief  超声波采集任务入口
 * @param  argument  未使用
 * @note   周期：从EEPROM配置 ultrasonic_period_ms 读取，默认500ms
 *         循环：测量 → 更新快照 → 串口打印 → 可选LED控制 → osDelay
 */
void APP_HC_SR04_Task(void *argument);

#endif /* APP_HC_SR04_H */
