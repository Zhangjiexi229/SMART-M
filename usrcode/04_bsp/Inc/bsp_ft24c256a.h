/**
 ******************************************************************************
 * @file    bsp_ft24c256a.h
 * @brief   FT24C256A EEPROM 驱动头文件 — 32KB I2C EEPROM，软件I2C
 *
 *  硬件映射：
 *    FT24C_SCL -> PC6  (推挽输出)
 *    FT24C_SDA -> PC8  (开漏输出，需外部上拉4.7K)
 *    WP        -> GND  (写保护禁用，允许读写)
 *    A2/A1/A0  -> GND  (I2C 7位地址 = 0x50)
 *
 *  芯片规格：
 *    - 容量：256Kbit = 32KB (地址范围 0x0000~0x7FFF)
 *    - 页大小：64字节（页写不可跨页边界）
 *    - 写周期：典型3ms / 最大5ms（内部编程时间）
 *    - 工作频率：100kHz(1.8V) / 400kHz(5V)
 *    - 写寿命：约100万次
 *
 *  设计原则：
 *    - BSP层不依赖RTOS，保证可在裸机环境使用
 *    - 软件I2C，开漏SDA支持ACK检测
 *    - 写操作自动分页 + 自动等待写完成，上层无需关心页边界
 ******************************************************************************
 */
#ifndef BSP_FT24C256A_H
#define BSP_FT24C256A_H

#include <stdint.h>

/* ========== 芯片参数 ========== */
#define FT24C_I2C_ADDR        0xA0U   /* 8位写地址（7位地址0x50 << 1） */
#define FT24C_PAGE_SIZE       64U     /* 页大小（字节） */
#define FT24C_TOTAL_SIZE      32768U  /* 总容量 32KB */
#define FT24C_WRITE_TIMEOUT   10U     /* 写完成轮询超时（ms） */

/**
 * @brief  初始化 FT24C256A 软件I2C引脚
 * @note   配置 PC6=SCL推挽输出, PC8=SDA开漏输出；调用前需已使能GPIOC时钟
 */
void BSP_FT24C_Init(void);

/**
 * @brief  从指定地址读取数据（随机地址读，支持跨页连续读）
 * @param  addr   EEPROM 起始地址 (0~0x7FFF)
 * @param  buf    数据缓冲区
 * @param  len    读取字节数
 * @retval 0=成功, 非0=失败（I2C通信错误或地址越界）
 */
uint8_t BSP_FT24C_Read(uint16_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  向指定地址写入数据（自动分页 + 自动等待写完成）
 * @param  addr   EEPROM 起始地址 (0~0x7FFF)
 * @param  buf    数据缓冲区
 * @param  len    写入字节数
 * @retval 0=成功, 非0=失败（I2C通信错误、地址越界或写超时）
 * @note   自动按64字节页边界拆分，每页写完后轮询等待内部编程完成
 */
uint8_t BSP_FT24C_Write(uint16_t addr, const uint8_t *buf, uint32_t len);

/**
 * @brief  检查 EEPROM 是否就绪（通信测试）
 * @retval 0=就绪, 非0=无响应
 */
uint8_t BSP_FT24C_CheckReady(void);

#endif /* BSP_FT24C256A_H */
