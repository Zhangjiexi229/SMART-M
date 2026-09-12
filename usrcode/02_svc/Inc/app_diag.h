/**
 ******************************************************************************
 * @file    app_diag.h
 * @brief   诊断数据中枢头文件 — 浮点传感器快照 + 全局告警标志
 *
 *  定位：
 *    统一快照（app_sensor.h）以 int/dec 拆分格式服务 MQTT 物模型上报；
 *    本模块以 float 格式集中存放"诊断用"数据（告警阈值比较、振动特征、
 *    故障诊断、OLED 显示、断网补传），由各传感器任务喂入。
 *
 *  数据流：
 *    SHT30任务   -> APP_DIAG_UpdateTempHum()    （温度℃/湿度%RH）
 *    QMI8658任务 -> APP_DIAG_UpdateVibration()  （振动模值g，含静态1g）
 *    INA226任务  -> APP_DIAG_UpdatePower()      （电流A/电压V/功率W）
 *    告警任务     -> g_alarm_active（读）        （OLED闪烁/MQTT告警字段）
 *
 *  并发安全：快照读写用 FreeRTOS 临界区保护；g_alarm_active 为 volatile
 ******************************************************************************
 */
#ifndef APP_DIAG_H
#define APP_DIAG_H

#include <stdint.h>

/* ==========================================================================
 *  诊断快照
 * ========================================================================== */
typedef struct {
    float temperature;   /*!< SHT30 温度（℃，可负） */
    float humidity;      /*!< SHT30 湿度（%RH） */
    float vibration;     /*!< QMI8658 振动模值 sqrt(ax^2+ay^2+az^2)，单位g（含静态1g） */
    float current;       /*!< INA226 电流（A） */
    float voltage;       /*!< INA226 母线电压（V） */
    float power;         /*!< INA226 功率（W） */
    uint8_t sht_valid;   /*!< SHT30 数据有效 */
    uint8_t imu_valid;   /*!< QMI8658 数据有效 */
    uint8_t ina_valid;   /*!< INA226 数据有效 */
} APP_Diag_Snapshot_t;

/* 全局告警标志：1=告警中（OLED闪烁、MQTT告警字段、断网补传标记），0=正常 */
extern volatile uint8_t g_alarm_active;

/* ==========================================================================
 *  API
 * ========================================================================== */

/**
 * @brief  更新SHT30温湿度（由 app_sht30 任务采集成功后调用）
 * @param  temperature  温度（℃，float）
 * @param  humidity     湿度（%RH，float）
 * @param  valid        数据有效标志
 */
void APP_DIAG_UpdateTempHum(float temperature, float humidity, uint8_t valid);

/**
 * @brief  更新QMI8658振动模值（由 app_qmi8658 任务采集成功后调用）
 * @param  vibration  振动模值（g，sqrt(ax^2+ay^2+az^2)）
 * @param  valid      数据有效标志
 */
void APP_DIAG_UpdateVibration(float vibration, uint8_t valid);

/**
 * @brief  更新INA226电源数据（由 app_ina226 任务采集成功后调用）
 * @param  current  电流（A）
 * @param  voltage  母线电压（V）
 * @param  power    功率（W）
 * @param  valid    数据有效标志
 */
void APP_DIAG_UpdatePower(float current, float voltage, float power, uint8_t valid);

/**
 * @brief  获取诊断快照（线程安全）
 * @param  out  输出快照指针（不可为NULL）
 */
void APP_DIAG_GetSnapshot(APP_Diag_Snapshot_t *out);

/**
 * @brief  全量无效化（读取失败时保留上次数值，仅清valid）
 */
void APP_DIAG_Invalidate(void);

#endif /* APP_DIAG_H */
