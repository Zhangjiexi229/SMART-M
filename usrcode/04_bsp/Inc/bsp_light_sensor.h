/**
 ******************************************************************************
 * @file    bsp_light_sensor.h
 * @brief   光敏传感器驱动头文件 — ADC3_IN5(PF7) 查询式单次采集
 *
 *  硬件电路（固定电阻上拉、光敏电阻接地）：
 *    VDD3V3 -- R36(固定电阻) --+-- R35(限流) -- PF7/ADC3_IN5
 *                               |
 *                               R37(光敏电阻)
 *                               |
 *                              GND
 *
 *  特性：光敏电阻接GND，光照越强阻值越小 → 分压点电压越低 → ADC值越小
 *    强光(灯光照射) ≈ 150
 *    弱光(手挡住)   ≈ 900
 *
 *  设计原则：
 *    - BSP层不依赖RTOS，保证可在裸机环境使用
 *    - 仅依赖STM32 HAL库，不直接操作寄存器
 *    - 向上层提供统一API，换硬件只需修改本驱动
 ******************************************************************************
 */
#ifndef BSP_LIGHT_SENSOR_H
#define BSP_LIGHT_SENSOR_H

#include <stdint.h>

#define LIGHT_SENSOR_ADC_MAX_VALUE   4095U   /*!< 12位ADC最大值 */
#define LIGHT_SENSOR_VREF_MV         3300U   /*!< 参考电压(mV)，VDD3V3 */

/**
 * @brief  光敏传感器初始化
 * @note   ADC3基础初始化由CubeMX的MX_ADC3_Init()完成；
 *         本函数预留扩展（如校准、首次转换预热）
 */
void BSP_LightSensor_Init(void);

/**
 * @brief  读取一次光敏传感器原始ADC值（查询式，阻塞约几us~几十us）
 * @return 12位ADC原始值，范围 0~4095；失败返回0
 */
uint16_t BSP_LightSensor_ReadRaw(void);

/**
 * @brief  读取一次光敏传感器电压值
 * @return 分压点电压，单位 V
 */
float BSP_LightSensor_ReadVoltage(void);

/**
 * @brief  读取一次光敏传感器电压值（整数mV，避免浮点）
 * @return 分压点电压，单位 mV
 */
uint32_t BSP_LightSensor_ReadMilliVolt(void);

#endif /* BSP_LIGHT_SENSOR_H */
