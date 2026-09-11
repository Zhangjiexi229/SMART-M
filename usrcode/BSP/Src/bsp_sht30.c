/**
 ******************************************************************************
 * @file    bsp_sht30.c
 * @brief   SHT30 温湿度传感器驱动实现 — 共享软件I2C（bsp_i2c_soft）
 *
 *  ▍单次测量流程：
 *    1. 发送命令 0x2C 0x06（单次测量，高重复性）
 *    2. 等待 20ms（临界区外，不阻塞I2C总线，允许其他器件通信）
 *    3. 读取 6 字节：T_MSB T_LSB T_CRC RH_MSB RH_LSB RH_CRC
 *    4. CRC8 校验（多项式0x31，初值0xFF），通过后换算物理量
 *
 *  ▍换算公式（Sensirion 官方）：
 *    Temp(℃) = -45 + 175 * raw / 65535
 *    RH(%)    = 100 * raw / 65535
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_SHT30_ENABLE

#include "bsp_sht30.h"
#include "bsp_i2c_soft.h"
#include "bsp_delay.h"
#include "bsp_uart.h"
#include <stddef.h>

/* ========== CRC8 校验（多项式 0x31，初值 0xFF） ========== */

/**
 * @brief  计算 SHT30 CRC8 校验值
 * @param  data  数据指针
 * @param  len   数据长度
 * @retval CRC8 校验值
 */
static uint8_t SHT30_CalcCRC8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFFU;
    uint8_t i, j;

    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (j = 0U; j < 8U; j++) {
            if (crc & 0x80U) {
                crc = (uint8_t)((crc << 1U) ^ 0x31U);
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }
    return crc;
}

/* ========== 公共 API ========== */

uint8_t BSP_SHT30_CheckReady(void)
{
    return BSP_I2C_Soft_Probe(SHT30_I2C_ADDR);
}

uint8_t BSP_SHT30_Init(void)
{
    uint8_t cmd[2];

    /* 1. 探测器件 */
    if (BSP_SHT30_CheckReady() != 0U) {
        return 1U;
    }

    /* 2. 软件复位（0x30 0xA2），复位后寄存器恢复默认 */
    cmd[0] = SHT30_CMD_SOFT_RESET;
    cmd[1] = SHT30_CMD_SOFT_RESET_2;
    (void)BSP_I2C_Soft_WriteRegs(SHT30_I2C_ADDR, cmd[0], &cmd[1], 1U);
    BSP_DelayMs(2U);   /* 复位完成时间 1.5ms（典型） */

    /* 3. 清状态寄存器（0x30 0x41），清除告警标志 */
    cmd[0] = SHT30_CMD_CLEAR_STATUS;
    cmd[1] = SHT30_CMD_CLEAR_STATUS_2;
    (void)BSP_I2C_Soft_WriteRegs(SHT30_I2C_ADDR, cmd[0], &cmd[1], 1U);

    return 0U;
}

uint8_t BSP_SHT30_Read(BSP_SHT30_Data_t *data)
{
    uint8_t cmd[2];
    uint8_t buf[6];
    uint16_t temp_raw, hum_raw;
    uint32_t temp_x100, hum_x100;
    int32_t  temp_c;

    if (data == NULL) {
        return 1U;
    }

    /* 1. 发送单次测量命令（高重复性） */
    cmd[0] = SHT30_CMD_SINGLE_HIGH;
    cmd[1] = SHT30_CMD_SINGLE_HIGH_2;
    if (BSP_I2C_Soft_WriteRegs(SHT30_I2C_ADDR, cmd[0], &cmd[1], 1U) != 0U) {
        BSP_UART1_Printf("[SHT30] err: write cmd failed\r\n");
        return 1U;
    }

    /* 2. 等待测量完成（临界区外） */
    BSP_DelayMs(SHT30_MEASURE_DELAY_MS);

    /* 3. 读取 6 字节数据（官方时序：发命令→等待→直接读，无需寄存器地址） */
    if (BSP_I2C_Soft_Read(SHT30_I2C_ADDR, buf, 6U) != 0U) {
        BSP_UART1_Printf("[SHT30] err: read 6B failed\r\n");
        return 1U;
    }

    /* 诊断：打印读到的 6 字节 */
    BSP_UART1_Printf("[SHT30] raw: %02X %02X %02X %02X %02X %02X\r\n",
        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    /* 4. CRC8 校验（温度2字节 + 湿度2字节） */
    if (SHT30_CalcCRC8(&buf[0], 2U) != buf[2]) {
        BSP_UART1_Printf("[SHT30] err: temp CRC mismatch (calc=%02X read=%02X)\r\n",
            SHT30_CalcCRC8(&buf[0], 2U), buf[2]);
        return 1U;
    }
    if (SHT30_CalcCRC8(&buf[3], 2U) != buf[5]) {
        BSP_UART1_Printf("[SHT30] err: hum CRC mismatch (calc=%02X read=%02X)\r\n",
            SHT30_CalcCRC8(&buf[3], 2U), buf[5]);
        return 1U;
    }

    /* 5. 原始值 */
    temp_raw = (uint16_t)(((uint16_t)buf[0] << 8U) | buf[1]);
    hum_raw  = (uint16_t)(((uint16_t)buf[3] << 8U) | buf[4]);

    /* 6. 换算物理量：temp = -45 + 175*raw/65535，保留一位小数
     *    temp_x100 = (175*raw/65535 - 45) * 100，整数运算 */
    temp_x100 = ((uint32_t)temp_raw * 17500U) / 65535U;           /* 175*100 = 17500 */
    if (temp_x100 >= 4500U) {
        temp_c = (int32_t)temp_x100 - 4500;                        /* 正值：4500 对应 0℃ */
    } else {
        temp_c = -((int32_t)4500 - (int32_t)temp_x100);            /* 负值 */
    }
    data->temperature_int = (int16_t)(temp_c / 100);
    data->temperature_dec = (uint8_t)((temp_c % 100) / 10);
    if (data->temperature_dec > 9U) {
        data->temperature_dec = 9U;
    }

    /* 湿度：hum = 100*raw/65535，保留一位小数 */
    hum_x100 = ((uint32_t)hum_raw * 10000U) / 65535U;
    data->humidity_int = (uint8_t)(hum_x100 / 100U);
    data->humidity_dec = (uint8_t)((hum_x100 % 100U) / 10U);
    if (data->humidity_int > 100U) {
        data->humidity_int = 100U;
    }

    return 0U;
}

#endif /* BSP_SHT30_ENABLE */
