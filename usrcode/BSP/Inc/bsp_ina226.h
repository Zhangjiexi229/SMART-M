/**
 ******************************************************************************
 * @file    bsp_ina226.h
 * @brief   INA226 电源监测驱动头文件 — 软件I2C（共享总线 bsp_i2c_soft）
 *
 *  硬件连接：
 *    SDA -> PB9（板载IIC排针 SDA）
 *    SCL -> PB8（板载IIC排针 SCL）
 *    A0/A1 -> 模块出厂固定接 VS（7位地址 = 0x45，本模块实测；见下方地址表）
 *    VS  -> 被测电源正极（电机驱动电源，经0.1Ω采样电阻）
 *    VBUS-> 被测母线电压（直接接母线）
 *    IN+/IN- -> 0.1Ω 采样电阻两端
 *
 *  寄存器（TI INA226，16位）：
 *    CFG(0x00)         配置：AVG=000(1次), VBUSCT=111(8.244ms),
 *                      VSHCT=111(8.244ms), MODE=111(连续shunt+bus)
 *    SHUNT_VOLTAGE(0x01)  分流电压，LSB=2.5uV
 *    BUS_VOLTAGE(0x02)    母线电压，LSB=1.25mV（右移3位后×1.25）
 *    POWER(0x03)          功率，LSB = 25 * Current_LSB
 *    CURRENT(0x04)        电流，LSB = 用户设定 Current_LSB
 *    CALIBRATION(0x05)    校准寄存器 = 0.00512 / (Current_LSB * Rshunt)
 *
 *  本驱动参数（可按实际采样电阻修改）：
 *    Rshunt = 0.1Ω，Current_LSB = 0.5mA
 *    CAL = 0.00512 / (0.0005 * 0.1) = 102.4 → 取 102
 *    电流满量程 = 0.5mA * 32768 = 16.38A（远大于电机工作电流）
 ******************************************************************************
 */
#ifndef BSP_INA226_H
#define BSP_INA226_H

#include <stdint.h>

/* ========== 芯片参数 ========== */
/* 本模块（DFRobot SEN0291 款）A0/A1 引脚出厂固定接 VS(3.3V) → 7位地址 = 0x45
 * INA226 地址表（TI 手册 Table 5）：
 *   A1=GND A0=GND → 0x40；A1=VS A0=VS → 0x45；A1=SDA A0=SCL → 0x4F 等
 * 若换模块，按扫描结果（[I2C-Scan] found 0xXX）同步修改本宏。 */
#define INA226_I2C_ADDR         0x45U   /*!< 7位I2C地址（本模块实测 0x45） */
#define INA226_RSHUNT_OHM       0.1f    /*!< 采样电阻（Ω），如需更换请同步修改 */
#define INA226_CURRENT_LSB_A    0.0005f /*!< 电流LSB（A）= 0.5mA */

/* ========== 寄存器地址 ========== */
#define INA226_REG_CFG          0x00U
#define INA226_REG_SHUNT_VOLT   0x01U
#define INA226_REG_BUS_VOLT     0x02U
#define INA226_REG_POWER        0x03U
#define INA226_REG_CURRENT      0x04U
#define INA226_REG_CALIBRATION  0x05U

/* ========== 配置值 ========== */
/* CFG 位域（TI INA226 手册 Table 2，POR=0x4127 为触发模式，须显式写连续模式）：
 *   AVG[12:10]    = 000  -> 1 次平均（软件端已有滑动滤波）
 *   VBUSCT[9:7]   = 111  -> 母线转换时间 8.244ms
 *   VSHCT[6:4]    = 111  -> 分流转换时间 8.244ms
 *   MODE[3:1]     = 111  -> 连续测量：shunt → bus → 自动算电流/功率，循环刷新
 *   0x3FE = 0b0_0000_111_111_111_0 */
#define INA226_CFG_DEFAULT      0x3FEU /*!< 连续模式，shunt/bus均8.244ms，1次平均 */
#define INA226_CAL_VALUE        102U    /*!< = 0.00512/(0.0005*0.1)，见头文件说明 */

/* 诊断：BSP_INA226_Init 最后一次 CFG 回读值（init 返回 5 时，用它看模块实际返回值） */
extern uint16_t BSP_INA226_LastCfgReadback;

/**
 * @brief  INA226 电源数据（全部为整数工程值）
 */
typedef struct {
    uint16_t bus_mv;        /*!< 母线电压（mV） */
    int16_t  current_ma;    /*!< 电流（mA，负值=反向电流） */
    uint32_t power_mw;      /*!< 功率（mW） */
    int16_t  shunt_uv;      /*!< 分流电阻电压（uV，诊断用） */
} BSP_INA226_Data_t;

/**
 * @brief  INA226 初始化（探测 + 配置校准与连续模式）
 * @retval 0=初始化成功，非0=失败
 * @note   前置条件：BSP_I2C_Soft_Init() 已调用
 */
uint8_t BSP_INA226_Init(void);

/**
 * @brief  读取一次电源数据（电压/电流/功率）
 * @param  data  输出参数
 * @retval 0=成功，非0=失败
 */
uint8_t BSP_INA226_Read(BSP_INA226_Data_t *data);

/**
 * @brief  探测 INA226 是否在线
 * @retval 0=在线，非0=无应答
 */
uint8_t BSP_INA226_CheckReady(void);

#endif /* BSP_INA226_H */
