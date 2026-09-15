/**
 ******************************************************************************
 * @file    app_sd.h
 * @brief   SD 卡数据存储应用 — FatFs 挂载 + 周期 CSV 数据落盘
 *
 *  功能：
 *    1. 上电自动初始化 SDIO + 挂载 FAT 文件系统（失败周期重试）；
 *    2. 每 5s 把诊断快照（温度/湿度/振动/电流/电压/功率/告警）追加写入
 *       /SMART/LOGxxxx.CSV（单文件上限 256KB 自动滚动新建）；
 *    3. 提供 APP_SD_AppendLine() 供其他任务写入事件行（告警/状态等）。
 *
 *  文件格式（CSV）：
 *    uptime_s,temp_c,hum_rh,vib_g,current_a,voltage_v,power_w,alarm
 ******************************************************************************
 */
#ifndef APP_SD_H
#define APP_SD_H

#include <stdint.h>

/**
 * @brief  SD 卡是否已挂载就绪（供 OLED/告警等模块显示状态）
 * @retval 1 就绪；0 未就绪
 */
uint8_t APP_SD_IsReady(void);

/**
 * @brief  格式化 SD 卡为 FAT32（危险操作：清空卡上数据）
 * @note   格式化后自动重新挂载；调用前请确保已确认用户意图
 * @retval 0 成功；非 0 失败
 */
uint8_t APP_SD_Format(void);

/**
 * @brief  追加一行文本到当前日志文件（线程安全）
 * @param  line 以 '\n' 结尾的文本行（长度 < 200）
 * @retval 0 成功；非 0 失败
 */
uint8_t APP_SD_AppendLine(const char *line);

/**
 * @brief  SD 数据存储任务（FreeRTOS）
 */
void APP_SD_Task(void *argument);

#endif /* APP_SD_H */
