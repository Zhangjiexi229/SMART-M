/**
 ******************************************************************************
 * @file    app_offline.h
 * @brief   断网补传缓冲头文件 — WiFi断开时缓存数据，恢复后批量补发
 *
 *  对应工作任务 Day6「断网补传机制（选做）」：
 *    - WiFi 断开/发布失败时，传感器数据存入 RAM 环形缓冲区
 *    - 网络恢复后由 MQTT 任务批量补发（见 app_mqtt.c）
 *    - 缓冲区满时覆盖最旧数据
 *
 *  容量：300 条 × 六元组 float ≈ 7.2KB RAM；2秒一条 ≈ 10分钟断网覆盖
 ******************************************************************************
 */
#ifndef APP_OFFLINE_H
#define APP_OFFLINE_H

#include <stdint.h>
#include "app_diag.h"

#define APP_OFFLINE_BUF_SIZE   300U   /* 存300条，2秒一条≈10分钟 */

/* 环形缓冲区 */
typedef struct {
    APP_Diag_Snapshot_t buffer[APP_OFFLINE_BUF_SIZE];
    uint16_t head;    /* 写入位置 */
    uint16_t tail;    /* 读取位置 */
    uint16_t count;   /* 当前条数 */
} OfflineBuffer_t;

/**
 * @brief  缓冲初始化（清零）
 */
void APP_OFFLINE_Init(void);

/**
 * @brief  存入一条快照（缓冲区满时覆盖最旧数据）
 * @param  snap  快照指针（只取六元组数值字段）
 * @retval 1=成功
 */
uint8_t APP_OFFLINE_Push(const APP_Diag_Snapshot_t *snap);

/**
 * @brief  取出一条最旧快照（供补发）
 * @param  snap  输出快照
 * @retval 1=成功，0=空
 */
uint8_t APP_OFFLINE_Pop(APP_Diag_Snapshot_t *snap);

/**
 * @brief  当前缓存条数
 * @retval 0~APP_OFFLINE_BUF_SIZE
 */
uint16_t APP_OFFLINE_Count(void);

#endif /* APP_OFFLINE_H */
