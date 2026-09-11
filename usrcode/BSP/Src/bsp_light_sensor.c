/**
 ******************************************************************************
 * @file    bsp_light_sensor.c
 * @brief   光敏传感器驱动实现 — ADC3_IN5(PF7) 查询式单次转换
 *
 *  采集流程：
 *    HAL_ADC_Start() → 软件触发单次转换 →
 *    HAL_ADC_PollForConversion() 等待EOC →
 *    HAL_ADC_GetValue() 读取DR寄存器 →
 *    HAL_ADC_Stop() 停止
 *
 *  注意：
 *    - 查询式阻塞，单次转换时间 ≈ (采样周期+12) / ADCCLK
 *      84MHz/2=42MHz ADCCLK，采样84周期 → (84+12)/42M ≈ 2.3us
 *    - 不使用DMA和中断，逻辑简单，适合低速周期采集场景
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_LIGHT_SENSOR_ENABLE

#include "bsp_light_sensor.h"
#include "main.h"
#include "adc.h"

/* ========== 外部声明（不修改MX文件，直接在此声明） ========== */
extern ADC_HandleTypeDef hadc3;

/**
 * @brief  光敏传感器初始化
 * @note   ADC3句柄初始化由CubeMX生成的MX_ADC3_Init()在main中调用；
 *         此处做一次空转换预热，确保首次读取稳定
 */
void BSP_LightSensor_Init(void)
{
    /* 预热：丢弃第一次转换结果，消除内部采样电容初始状态影响 */
    (void)BSP_LightSensor_ReadRaw();
}

/**
 * @brief  读取一次原始ADC值（查询式阻塞）
 * @return 12位ADC值 0~4095；超时或失败返回0
 */
uint16_t BSP_LightSensor_ReadRaw(void)
{
    uint16_t value = 0U;

    if (HAL_ADC_Start(&hadc3) != HAL_OK) {
        return 0U;
    }

    /* 等待转换完成，超时10ms（远大于实际转换时间） */
    if (HAL_ADC_PollForConversion(&hadc3, 10U) == HAL_OK) {
        value = (uint16_t)HAL_ADC_GetValue(&hadc3);
    }

    (void)HAL_ADC_Stop(&hadc3);
    return value;
}

/**
 * @brief  读取电压值（浮点）
 * @return 电压，单位 V
 */
float BSP_LightSensor_ReadVoltage(void)
{
    uint16_t raw = BSP_LightSensor_ReadRaw();
    return (float)raw * (float)LIGHT_SENSOR_VREF_MV / 1000.0f
           / (float)LIGHT_SENSOR_ADC_MAX_VALUE;
}

/**
 * @brief  读取电压值（整数mV，避免浮点库开销）
 * @return 电压，单位 mV
 */
uint32_t BSP_LightSensor_ReadMilliVolt(void)
{
    uint16_t raw = BSP_LightSensor_ReadRaw();
    return (uint32_t)raw * LIGHT_SENSOR_VREF_MV / LIGHT_SENSOR_ADC_MAX_VALUE;
}

#endif /* BSP_LIGHT_SENSOR_ENABLE */
