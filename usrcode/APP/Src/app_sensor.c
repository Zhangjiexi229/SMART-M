/**
 ******************************************************************************
 * @file    app_sensor.c
 * @brief   统一传感器快照层实现 — 集中存放所有传感器最近一次有效数据
 *
 *  读写并发保护：全部使用 FreeRTOS 临界区（taskENTER_CRITICAL），
 *  与 app_dht11 快照、app_light_sensor 快照的实现风格一致。
 ******************************************************************************
 */
#include "module_cfg.h"
#include "app_sensor.h"
#include "cmsis_os.h"

#if APP_SENSOR_ENABLE

/* ==========================================================================
 *  统一快照（静态全局，各传感器任务写，MQTT任务读）
 * ========================================================================== */
static APP_Sensor_Snapshot_t s_snapshot = {0};

void APP_SENSOR_GetSnapshot(APP_Sensor_Snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL();
    *out = s_snapshot;
    taskEXIT_CRITICAL();
}

void APP_SENSOR_UpdateTempHum(uint8_t t_int, uint8_t t_dec,
                              uint8_t h_int, uint8_t h_dec, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.dht_temp_int = t_int;
    s_snapshot.dht_temp_dec = t_dec;
    s_snapshot.dht_hum_int  = h_int;
    s_snapshot.dht_hum_dec  = h_dec;
    s_snapshot.dht_valid    = valid;
    taskEXIT_CRITICAL();
}

void APP_SENSOR_UpdateSHT30(int16_t temp_int, uint8_t temp_dec,
                            uint8_t hum_int, uint8_t hum_dec, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.sht_temp_int = temp_int;
    s_snapshot.sht_temp_dec = temp_dec;
    s_snapshot.sht_hum_int  = hum_int;
    s_snapshot.sht_hum_dec  = hum_dec;
    s_snapshot.sht_valid    = valid;
    taskEXIT_CRITICAL();
}

void APP_SENSOR_UpdateIMU(int16_t pitch_int, uint8_t pitch_dec,
                          int16_t roll_int,  uint8_t roll_dec,
                          int16_t ax_mg, int16_t ay_mg, int16_t az_mg,
                          int16_t gx_mdps, int16_t gy_mdps, int16_t gz_mdps,
                          uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.imu_pitch_int = pitch_int;
    s_snapshot.imu_pitch_dec = pitch_dec;
    s_snapshot.imu_roll_int  = roll_int;
    s_snapshot.imu_roll_dec  = roll_dec;
    s_snapshot.imu_ax_mg     = ax_mg;
    s_snapshot.imu_ay_mg     = ay_mg;
    s_snapshot.imu_az_mg     = az_mg;
    s_snapshot.imu_gx_mdps   = gx_mdps;
    s_snapshot.imu_gy_mdps   = gy_mdps;
    s_snapshot.imu_gz_mdps   = gz_mdps;
    s_snapshot.imu_valid     = valid;
    taskEXIT_CRITICAL();
}

void APP_SENSOR_UpdatePower(uint16_t bus_mv, int16_t current_ma,
                            uint32_t power_mw, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.pwr_bus_mv     = bus_mv;
    s_snapshot.pwr_current_ma = current_ma;
    s_snapshot.pwr_power_mw   = power_mw;
    s_snapshot.pwr_valid      = valid;
    taskEXIT_CRITICAL();
}

void APP_SENSOR_UpdateLight(uint16_t adc, uint16_t mv)
{
    taskENTER_CRITICAL();
    s_snapshot.light_adc = adc;
    s_snapshot.light_mv  = mv;
    taskEXIT_CRITICAL();
}

void APP_SENSOR_UpdateRelay(uint8_t on, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.relay_on    = on;
    s_snapshot.relay_valid = valid;
    taskEXIT_CRITICAL();
}

#endif /* APP_SENSOR_ENABLE */
