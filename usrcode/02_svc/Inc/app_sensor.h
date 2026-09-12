/**
 ******************************************************************************
 * @file    app_sensor.h
 * @brief   统一传感器快照层头文件 — 各传感器任务写，MQTT/WiFi 任务读
 *
 *  设计说明：
 *    - 所有传感器的最近一次有效数据集中存放在一个快照结构体中，
 *      读取方（MQTT上报）只需一次临界区读取即可拿到全部数据
 *    - 各传感器任务在采集成功后调用 APP_SENSOR_UpdateXXX() 更新对应字段
 *    - 快照读写均用 FreeRTOS 临界区保护（taskENTER_CRITICAL），线程安全
 *    - 快照接口始终可用；模块未启用时对应字段 valid=0，读取方忽略
 *
 *  数据来源：
 *    DHT11   -> APP_SENSOR_UpdateTempHum()   （温湿度，int/dec）
 *    SHT30   -> APP_SENSOR_UpdateSHT30()     （温湿度，int/dec，温度可负）
 *    QMI8658 -> APP_SENSOR_UpdateIMU()       （姿态角 + 六轴）
 *    INA226  -> APP_SENSOR_UpdatePower()     （母线电压/电流/功率）
 *    继电器   -> APP_SENSOR_UpdateRelay()     （吸合状态）
 ******************************************************************************
 */
#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include <stdint.h>

/* ==========================================================================
 *  统一传感器快照
 * ========================================================================== */
typedef struct {
    /* ---- DHT11 温湿度（int/dec 格式，温度非负） ---- */
    uint8_t  dht_temp_int;      /*!< DHT11 温度整数部分（℃） */
    uint8_t  dht_temp_dec;      /*!< DHT11 温度小数部分 */
    uint8_t  dht_hum_int;       /*!< DHT11 湿度整数部分（%RH） */
    uint8_t  dht_hum_dec;       /*!< DHT11 湿度小数部分 */
    uint8_t  dht_valid;         /*!< DHT11 数据有效标志 */

    /* ---- SHT30 温湿度（int/dec，温度可负） ---- */
    int16_t  sht_temp_int;      /*!< SHT30 温度整数部分（℃，含符号） */
    uint8_t  sht_temp_dec;      /*!< SHT30 温度小数部分（0~9） */
    uint8_t  sht_hum_int;       /*!< SHT30 湿度整数部分（%RH） */
    uint8_t  sht_hum_dec;       /*!< SHT30 湿度小数部分 */
    uint8_t  sht_valid;         /*!< SHT30 数据有效标志 */

    /* ---- QMI8658 姿态（pitch/roll 单位：度，保留1位小数） ---- */
    int16_t  imu_pitch_int;     /*!< 俯仰角整数部分（度，含符号） */
    uint8_t  imu_pitch_dec;     /*!< 俯仰角小数部分 */
    int16_t  imu_roll_int;      /*!< 横滚角整数部分（度，含符号） */
    uint8_t  imu_roll_dec;      /*!< 横滚角小数部分 */
    int16_t  imu_ax_mg;         /*!< 加速度X（mg，±16000） */
    int16_t  imu_ay_mg;         /*!< 加速度Y（mg） */
    int16_t  imu_az_mg;         /*!< 加速度Z（mg） */
    int16_t  imu_gx_mdps;       /*!< 角速度X（mdps，±128000） */
    int16_t  imu_gy_mdps;       /*!< 角速度Y（mdps） */
    int16_t  imu_gz_mdps;       /*!< 角速度Z（mdps） */
    uint8_t  imu_valid;         /*!< QMI8658 数据有效标志 */

    /* ---- INA226 电源 ---- */
    uint16_t pwr_bus_mv;        /*!< 母线电压（mV） */
    int16_t  pwr_current_ma;    /*!< 电流（mA） */
    uint32_t pwr_power_mw;      /*!< 功率（mW） */
    uint8_t  pwr_valid;         /*!< INA226 数据有效标志 */

    /* ---- 光敏（工程预留，app_light_sensor 调用） ---- */
    uint16_t light_adc;         /*!< 光敏ADC原始值 */
    uint16_t light_mv;          /*!< 光敏电压（mV） */

    /* ---- 继电器 ---- */
    uint8_t  relay_on;          /*!< 继电器状态：1=吸合(ON)，0=断开(OFF) */
    uint8_t  relay_valid;       /*!< 继电器状态有效标志 */
} APP_Sensor_Snapshot_t;

/* ==========================================================================
 *  API
 * ========================================================================== */

/**
 * @brief  获取统一传感器快照（线程安全）
 * @param  out  输出快照结构体指针（不可为NULL）
 * @note   未启用的传感器对应字段 valid=0
 */
void APP_SENSOR_GetSnapshot(APP_Sensor_Snapshot_t *out);

/**
 * @brief  更新DHT11温湿度（由 app_dht11 任务调用）
 */
void APP_SENSOR_UpdateTempHum(uint8_t t_int, uint8_t t_dec,
                              uint8_t h_int, uint8_t h_dec, uint8_t valid);

/**
 * @brief  更新SHT30温湿度（由 app_sht30 任务调用）
 */
void APP_SENSOR_UpdateSHT30(int16_t temp_int, uint8_t temp_dec,
                            uint8_t hum_int, uint8_t hum_dec, uint8_t valid);

/**
 * @brief  更新QMI8658姿态与六轴数据（由 app_qmi8658 任务调用）
 */
void APP_SENSOR_UpdateIMU(int16_t pitch_int, uint8_t pitch_dec,
                          int16_t roll_int,  uint8_t roll_dec,
                          int16_t ax_mg, int16_t ay_mg, int16_t az_mg,
                          int16_t gx_mdps, int16_t gy_mdps, int16_t gz_mdps,
                          uint8_t valid);

/**
 * @brief  更新INA226电源数据（由 app_ina226 任务调用）
 */
void APP_SENSOR_UpdatePower(uint16_t bus_mv, int16_t current_ma,
                            uint32_t power_mw, uint8_t valid);

/**
 * @brief  更新光敏数据（由 app_light_sensor 任务调用，工程预留）
 */
void APP_SENSOR_UpdateLight(uint16_t adc, uint16_t mv);

/**
 * @brief  更新继电器状态（由 app_relay 控制接口调用）
 */
void APP_SENSOR_UpdateRelay(uint8_t on, uint8_t valid);

#endif /* APP_SENSOR_H */
