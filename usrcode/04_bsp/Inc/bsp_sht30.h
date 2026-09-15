/**
 ******************************************************************************
 * @file    bsp_sht30.h
 * @brief   SHT30 温湿度传感器驱动头文件 — 软件I2C（共享总线 bsp_i2c_soft）
 *
 *  硬件连接：
 *    SDA -> PB7（共享I2C总线 SDA）
 *    SCL -> PB6（共享I2C总线 SCL）
 *    ADDR-> GND（7位地址 = 0x44）
 *
 *  芯片规格（Sensirion SHT30）：
 *    - 量程：温度 -40~125℃，湿度 0~100%RH
 *    - 精度：温度 ±0.2℃，湿度 ±2%RH（典型）
 *    - I2C 地址：0x44（ADDR=GND）/ 0x45（ADDR=VDD）
 *    - 单次测量高重复性：典型 15.5ms
 *    - 原始值转物理量：
 *        Temp(℃) = -45 + 175 * raw / 65535
 *        RH(%)    = 100 * raw / 65535
 *    - CRC8：多项式 0x31（x^8+x^5+x^4+1），初值 0xFF
 ******************************************************************************
 */
#ifndef BSP_SHT30_H
#define BSP_SHT30_H

#include <stdint.h>

/* ========== 芯片参数 ========== */
#define SHT30_I2C_ADDR          0x44U   /*!< 7位I2C地址（ADDR引脚接GND） */
#define SHT30_MEASURE_DELAY_MS  50U     /*!< 单次测量等待时间（高重复性典型15.5ms，取50ms余量） */

/* ========== SHT30 命令（2字节） ========== */
#define SHT30_CMD_SINGLE_HIGH   0x2CU   /*!< 单次测量，高重复性，时钟伸展关闭 */
#define SHT30_CMD_SINGLE_HIGH_2 0x06U
#define SHT30_CMD_SOFT_RESET    0x30U   /*!< 软件复位 */
#define SHT30_CMD_SOFT_RESET_2  0xA2U
#define SHT30_CMD_CLEAR_STATUS  0x30U   /*!< 清状态寄存器 */
#define SHT30_CMD_CLEAR_STATUS_2 0x41U

/**
 * @brief  SHT30 温湿度数据（整数+一位小数，温度可负）
 */
typedef struct {
    int16_t  temperature_int;   /*!< 温度整数部分（℃，含符号） */
    uint8_t  temperature_dec;   /*!< 温度小数部分（0~9，恒正） */
    uint8_t  humidity_int;      /*!< 湿度整数部分（%RH） */
    uint8_t  humidity_dec;      /*!< 湿度小数部分（0~9） */
} BSP_SHT30_Data_t;

/**
 * @brief  SHT30 初始化（探测器件 + 软件复位 + 清状态）
 * @retval 0=初始化成功（器件在线），非0=器件无响应
 * @note   前置条件：BSP_I2C_Soft_Init() 已调用
 */
uint8_t BSP_SHT30_Init(void);

/**
 * @brief  读取一次温湿度（单次测量模式，阻塞约20ms）
 * @param  data  输出参数，温湿度数据
 * @retval 0=成功（CRC校验通过），非0=失败
 * @note   测量等待期间不占用I2C总线（临界区外），调用间隔建议>=200ms
 */
uint8_t BSP_SHT30_Read(BSP_SHT30_Data_t *data);

/**
 * @brief  探测 SHT30 是否在线
 * @retval 0=在线，非0=无应答
 */
uint8_t BSP_SHT30_CheckReady(void);

#endif /* BSP_SHT30_H */
