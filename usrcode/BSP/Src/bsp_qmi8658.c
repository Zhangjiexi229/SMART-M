/**
 ******************************************************************************
 * @file    bsp_qmi8658.c
 * @brief   QMI8658A 六轴IMU驱动实现 — 共享软件I2C（bsp_i2c_soft）
 *
 *  ▍初始化流程（寄存器值依据官方手册考证，见 bsp_qmi8658.h）：
 *    1. 探测：读 WHO_AM_I(0x00) == 0x05
 *    2. CTRL1 = 0x40  地址自动递增 + 小端
 *    3. CTRL2 = 0x30  加速度 ±16g（2048 LSB/g）
 *    4. CTRL3 = 0x30  陀螺仪 ±128dps（256 LSB/dps）
 *    5. CTRL7 = 0x03  使能加速度计 + 陀螺仪
 *
 *  ▍读取流程：
 *    从 0x35 自动递增连续读 12 字节（小端，先低后高），
 *    前6字节 = 加速度 XYZ，后6字节 = 陀螺仪 XYZ。
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_QMI8658_ENABLE

#include "bsp_qmi8658.h"
#include "bsp_i2c_soft.h"
#include "bsp_delay.h"
#include <stddef.h>

/* ========== 公共 API ========== */

uint8_t BSP_QMI8658_CheckReady(void)
{
    uint8_t who = 0U;

    if (BSP_I2C_Soft_ReadRegs(QMI8658_I2C_ADDR, QMI8658_REG_WHO_AM_I, &who, 1U) != 0U) {
        return 1U;
    }
    return (who == QMI8658_WHO_AM_I_VAL) ? 0U : 1U;
}

uint8_t BSP_QMI8658_Init(void)
{
    /* 1. 探测器件 */
    if (BSP_QMI8658_CheckReady() != 0U) {
        return 1U;
    }

    /* 2. 接口配置：地址自动递增 + 小端字节序 */
    if (BSP_I2C_Soft_WriteReg(QMI8658_I2C_ADDR, QMI8658_REG_CTRL1, QMI8658_CTRL1_VAL) != 0U) {
        return 1U;
    }
    BSP_DelayMs(1U);

    /* 3. 加速度计：±16g */
    if (BSP_I2C_Soft_WriteReg(QMI8658_I2C_ADDR, QMI8658_REG_CTRL2, QMI8658_CTRL2_VAL) != 0U) {
        return 1U;
    }
    BSP_DelayMs(1U);

    /* 4. 陀螺仪：±128dps */
    if (BSP_I2C_Soft_WriteReg(QMI8658_I2C_ADDR, QMI8658_REG_CTRL3, QMI8658_CTRL3_VAL) != 0U) {
        return 1U;
    }
    BSP_DelayMs(1U);

    /* 5. 使能加速度计 + 陀螺仪 */
    if (BSP_I2C_Soft_WriteReg(QMI8658_I2C_ADDR, QMI8658_REG_CTRL7, QMI8658_CTRL7_VAL) != 0U) {
        return 1U;
    }
    BSP_DelayMs(5U);   /* 传感器上电稳定 */

    return 0U;
}

uint8_t BSP_QMI8658_Read(BSP_QMI8658_Data_t *data)
{
    uint8_t buf[12];

    if (data == NULL) {
        return 1U;
    }

    /* 从 0x35 自动递增连续读 12 字节 */
    if (BSP_I2C_Soft_ReadRegs(QMI8658_I2C_ADDR, QMI8658_REG_ACC_X_L, buf, 12U) != 0U) {
        return 1U;
    }

    /* 小端拼接（先低后高），有符号 */
    data->ax = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8U));
    data->ay = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8U));
    data->az = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8U));
    data->gx = (int16_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8U));
    data->gy = (int16_t)((uint16_t)buf[8] | ((uint16_t)buf[9] << 8U));
    data->gz = (int16_t)((uint16_t)buf[10] | ((uint16_t)buf[11] << 8U));

    return 0U;
}

#endif /* BSP_QMI8658_ENABLE */
