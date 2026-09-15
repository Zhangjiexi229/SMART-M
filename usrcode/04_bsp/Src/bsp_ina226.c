/**
 ******************************************************************************
 * @file    bsp_ina226.c
 * @brief   INA226 电源监测驱动实现 — 共享软件I2C（bsp_i2c_soft）
 *
 *  ▍换算公式（TI INA226）：
 *    bus_mv    = BUS_VOLTAGE_raw * 1.25        （无符号，LSB=1.25mV，最大 40.96V）
 *    current_ma= CURRENT_raw * Current_LSB_A * 1000
 *    power_mw  = POWER_raw * 25 * Current_LSB_A * 1000
 *    shunt_uv  = SHUNT_VOLTAGE_raw * 2.5
 *
 *  本驱动按 Rshunt=0.01Ω（新模块 R010）、Current_LSB=0.25mA、CAL=2048 配置。
 *  电流换算：Shunt(uV) / (Rshunt*1000) = I(mA)，0.01Ω 时直接 ÷10。
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_INA226_ENABLE

#include "bsp_ina226.h"
#include "bsp_i2c_soft.h"
#include <stddef.h>

/* ========== 内部工具 ========== */

/* 诊断：保存最后一次 CFG 回读值，供 APP 层打印（定位 init 失败阶段） */
uint16_t BSP_INA226_LastCfgReadback = 0U;

/**
 * @brief  读16位寄存器（大端，先高后低）
 * @param  reg  寄存器地址
 * @param  val  输出16位值
 * @retval 0=成功，非0=失败
 */
static uint8_t INA226_ReadReg16(uint8_t reg, uint16_t *val)
{
    uint8_t buf[2];

    if (BSP_I2C_Soft_ReadRegs(INA226_I2C_ADDR, reg, buf, 2U) != 0U) {
        return 1U;
    }
    *val = (uint16_t)(((uint16_t)buf[0] << 8U) | buf[1]);
    return 0U;
}

/**
 * @brief  写16位寄存器（大端）
 * @param  reg  寄存器地址
 * @param  val  16位值
 * @retval 0=成功，非0=失败
 */
static uint8_t INA226_WriteReg16(uint8_t reg, uint16_t val)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(val >> 8U);
    buf[1] = (uint8_t)(val & 0xFFU);
    return BSP_I2C_Soft_WriteRegs(INA226_I2C_ADDR, reg, buf, 2U);
}

/* ========== 公共 API ========== */

uint8_t BSP_INA226_CheckReady(void)
{
    return BSP_I2C_Soft_Probe(INA226_I2C_ADDR);
}

uint8_t BSP_INA226_Init(void)
{
    uint16_t cfg = 0U;

    /* 1. 探测器件 */
    if (BSP_INA226_CheckReady() != 0U) {
        return 1U;   /* 器件不应答（地址/接线/供电问题） */
    }

    /* 2. 写校准寄存器（先校准，电流/功率寄存器才有意义） */
    if (INA226_WriteReg16(INA226_REG_CALIBRATION, INA226_CAL_VALUE) != 0U) {
        return 2U;   /* 写 CAL 寄存器 NACK */
    }

    /* 3. 写配置寄存器（连续 shunt+bus 测量，8.244ms） */
    if (INA226_WriteReg16(INA226_REG_CFG, INA226_CFG_DEFAULT) != 0U) {
        return 3U;   /* 写 CFG 寄存器 NACK */
    }

    /* 4. 回读配置确认器件可读写 */
    if (INA226_ReadReg16(INA226_REG_CFG, &cfg) != 0U) {
        return 4U;   /* 回读 CFG 失败 */
    }
    BSP_INA226_LastCfgReadback = cfg;
    /* 只比较有效位：bit15 RST + AVG[11:9] + VBUSCT[8:6] + VSHCT[5:3] + MODE[2:0]
     * bit14/13/12 为保留位，读回值不保证为 0（本模块实测 bit14 读回为 1），不参与比较 */
    if ((cfg & 0x8FFFU) != (INA226_CFG_DEFAULT & 0x8FFFU)) {
        return 5U;   /* 回读有效位与写入不一致（模块异常/仿品） */
    }

    return 0U;
}

uint8_t BSP_INA226_Read(BSP_INA226_Data_t *data)
{
    uint16_t raw_shunt, raw_bus, raw_power, raw_current;

    if (data == NULL) {
        return 1U;
    }

    /* 依次读取4个数据寄存器（连续测量模式下数据实时更新） */
    if (INA226_ReadReg16(INA226_REG_SHUNT_VOLT, &raw_shunt) != 0U) {
        return 1U;
    }
    if (INA226_ReadReg16(INA226_REG_BUS_VOLT, &raw_bus) != 0U) {
        return 1U;
    }
    if (INA226_ReadReg16(INA226_REG_POWER, &raw_power) != 0U) {
        return 1U;
    }
    if (INA226_ReadReg16(INA226_REG_CURRENT, &raw_current) != 0U) {
        return 1U;
    }

    /* 母线电压：寄存器值直接 ×1.25mV（LSB=1.25mV，最大 0x7FFF → 40.96V，无需右移） */
    data->bus_mv = (uint16_t)(((uint32_t)raw_bus * 125U) / 100U);

    /* 分流电压：raw × 2.5uV（有符号） */
    data->shunt_uv = (int16_t)(((int32_t)(int16_t)raw_shunt * 25) / 10);

    /* 电流：直接用 Shunt 电压算（Rshunt=0.01Ω，I=V/R）
     * Shunt(uV) / 10 = I(mA)   （0.1Ω 模块时为 /100） */
    data->current_ma = (int16_t)(data->shunt_uv / 10);

    /* 功率：Bus(mV) * I(mA) / 1000 = P(mW) */
    data->power_mw = (uint16_t)(((uint32_t)data->bus_mv * (uint32_t)(data->current_ma > 0 ? data->current_ma : 0)) / 1000U);

    return 0U;
}

#endif /* BSP_INA226_ENABLE */
