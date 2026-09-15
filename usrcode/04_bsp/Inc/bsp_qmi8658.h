/**
 ******************************************************************************
 * @file    bsp_qmi8658.h
 * @brief   QMI8658A 六轴IMU驱动头文件 — 软件I2C（共享总线 bsp_i2c_soft）
 *
 *  硬件连接（**本模块**）：
 *    SDA -> PB7（共享I2C总线 SDA）
 *    SCL -> PB6（共享I2C总线 SCL）
 *    AD0/SA0 -> 板上固定接地（无引线）→ 7位地址 = 0x6B
 *    INT1/INT2 -> 悬空（本驱动轮询读取，不用中断）
 *
 *  ▍地址注意（QMI8658A/C Datasheet 5.6 节，官方原文）：
 *    SA0 悬空或接高电平（内部 200kΩ 上拉）→ 7位地址 = 0x6A
 *    SA0 接 GND                                        → 7位地址 = 0x6B
 *    本模块 AD0 出厂板上固定接地，故 QMI8658_I2C_ADDR = 0x6B；
 *    若换模块（AD0 悬空），须改回 0x6A。
 *
 *  寄存器配置（依据 QMI8658A Datasheet Rev.A 考证）：
 *    CTRL1(0x02) = 0x40 : bit6 ADDR_AI=1 地址自动递增, bit5 BE=0 小端读取
 *    CTRL2(0x03) = 0x30 : bit6:4 aFS=011 → 加速度 ±16g（灵敏度 2048 LSB/g）
 *                          bit3:0 aODR=0000（6DOF 最高ODR，由陀螺仪主导）
 *    CTRL3(0x04) = 0x30 : bit6:4 gFS=011 → 陀螺仪 ±128dps（灵敏度 256 LSB/dps）
 *                          bit3:0 gODR=0000（7174.4Hz）
 *    CTRL7(0x08) = 0x03 : bit1 gEN=1 使能陀螺仪, bit0 aEN=1 使能加速度计
 *
 *  数据寄存器（小端，先低后高）：
 *    A[X,Y,Z]_[H,L] = 0x35~0x3A  加速度原始值（LSB）
 *    G[X,Y,Z]_[H,L] = 0x3B~0x40  陀螺仪原始值（LSB）
 *
 *  物理量换算：
 *    accel(g) = raw / 2048.0   （±16g）
 *    gyro(dps) = raw / 256.0   （±128dps）
 ******************************************************************************
 */
#ifndef BSP_QMI8658_H
#define BSP_QMI8658_H

#include <stdint.h>

/* ========== 芯片参数 ========== */
#define QMI8658_I2C_ADDR        0x6BU   /*!< 7位I2C地址（本模块AD0板上固定接地→0x6B；换悬空模块须改0x6A） */

/* ========== 寄存器地址 ========== */
#define QMI8658_REG_WHO_AM_I    0x00U   /*!< 芯片ID，应为 0x05 */
#define QMI8658_WHO_AM_I_VAL    0x05U
#define QMI8658_REG_CTRL1       0x02U   /*!< 接口配置：地址自增 + 字节序 */
#define QMI8658_REG_CTRL2       0x03U   /*!< 加速度计：量程 + ODR */
#define QMI8658_REG_CTRL3       0x04U   /*!< 陀螺仪：量程 + ODR */
#define QMI8658_REG_CTRL7       0x08U   /*!< 传感器使能 */
#define QMI8658_REG_ACC_X_L     0x35U   /*!< 加速度X低字节（自动递增连续读） */

/* ========== 配置值 ========== */
#define QMI8658_CTRL1_VAL       0x40U   /*!< ADDR_AI=1自动递增, BE=0小端 */
#define QMI8658_CTRL2_VAL       0x30U   /*!< aFS=011(±16g), aODR=0000 */
#define QMI8658_CTRL3_VAL       0x30U   /*!< gFS=011(±128dps), gODR=0000 */
#define QMI8658_CTRL7_VAL       0x03U   /*!< gEN=1, aEN=1 */

/* ========== 灵敏度（LSB/物理量） ========== */
#define QMI8658_ACC_LSB_PER_G    2048    /*!< ±16g 量程灵敏度 */
#define QMI8658_GYR_LSB_PER_DPS  256     /*!< ±128dps 量程灵敏度 */

/**
 * @brief  QMI8658 六轴原始数据
 */
typedef struct {
    int16_t ax;   /*!< 加速度X（LSB，除以2048得g） */
    int16_t ay;   /*!< 加速度Y */
    int16_t az;   /*!< 加速度Z */
    int16_t gx;   /*!< 角速度X（LSB，除以256得dps） */
    int16_t gy;   /*!< 角速度Y */
    int16_t gz;   /*!< 角速度Z */
} BSP_QMI8658_Data_t;

/**
 * @brief  QMI8658 初始化（探测 + 配置寄存器）
 * @retval 0=初始化成功（WHO_AM_I校验通过），非0=失败
 * @note   前置条件：BSP_I2C_Soft_Init() 已调用
 */
uint8_t BSP_QMI8658_Init(void);

/**
 * @brief  读取一次六轴原始数据（自动递增连续读12字节）
 * @param  data  输出参数，六轴原始值
 * @retval 0=成功，非0=失败
 */
uint8_t BSP_QMI8658_Read(BSP_QMI8658_Data_t *data);

/**
 * @brief  探测 QMI8658 是否在线（读 WHO_AM_I）
 * @retval 0=在线且ID正确，非0=无应答或ID错误
 */
uint8_t BSP_QMI8658_CheckReady(void);

#endif /* BSP_QMI8658_H */
