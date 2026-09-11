/**
 ******************************************************************************
 * @file    app_diag.c
 * @brief   诊断数据中枢实现 — 浮点快照 + 全局告警标志
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_DIAG_ENABLE

#include "app_diag.h"
#include "FreeRTOS.h"
#include "task.h"

/* 全局告警标志（volatile，供OLED/MQTT/断网补传读取） */
volatile uint8_t g_alarm_active = 0U;

/* 诊断快照 */
static APP_Diag_Snapshot_t s_snapshot;

/* ==========================================================================
 *  API
 * ========================================================================== */
void APP_DIAG_UpdateTempHum(float temperature, float humidity, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.temperature = temperature;
    s_snapshot.humidity    = humidity;
    s_snapshot.sht_valid   = valid;
    taskEXIT_CRITICAL();
}

void APP_DIAG_UpdateVibration(float vibration, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.vibration = vibration;
    s_snapshot.imu_valid = valid;
    taskEXIT_CRITICAL();
}

void APP_DIAG_UpdatePower(float current, float voltage, float power, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.current = current;
    s_snapshot.voltage = voltage;
    s_snapshot.power   = power;
    s_snapshot.ina_valid = valid;
    taskEXIT_CRITICAL();
}

void APP_DIAG_GetSnapshot(APP_Diag_Snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL();
    *out = s_snapshot;
    taskEXIT_CRITICAL();
}

void APP_DIAG_Invalidate(void)
{
    taskENTER_CRITICAL();
    s_snapshot.sht_valid = 0U;
    s_snapshot.imu_valid = 0U;
    s_snapshot.ina_valid = 0U;
    taskEXIT_CRITICAL();
}

#endif /* APP_DIAG_ENABLE */
