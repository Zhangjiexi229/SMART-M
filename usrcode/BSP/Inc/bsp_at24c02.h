/**
 ******************************************************************************
 * @file    bsp_at24c02.h
 * @brief   I2C EEPROM 驱动头文件 — AT24C02/AIP24C02A（256字节，1字节地址，8字节页）
 *
 *  硬件映射：
 *    AT24C02_SCL -> PB8  (推挽输出，主机独占时钟线)
 *    AT24C02_SDA -> PB9  (动态切换：写数据时推挽输出，读ACK/数据时输入上拉)
 *    WP          -> GND  (写保护禁用，允许读写)
 *    A2/A1/A0    -> GND  (I2C 7位地址 = 0x50)
 *
 *  芯片规格：
 *    - 容量：2Kbit = 256字节 (地址范围 0x00~0xFF)
 *    - 页大小：8字节（页写不可跨页边界）
 *    - 写周期：典型3ms / 最大5ms（内部编程时间）
 *    - 工作频率：100kHz(1.8V) / 400kHz(5V)
 *    - 写寿命：约100万次
 *
 *  设计原则：
 *    - BSP层不依赖RTOS，保证可在裸机环境使用
 *    - SDA推挽输出写数据（不依赖外部上拉，边沿更陡更可靠）
 *    - 读ACK/数据时SDA切换为输入上拉（纯高阻释放总线）
 *    - 写操作自动分页 + 自动等待写完成，上层无需关心页边界
 ******************************************************************************
 */
#ifndef BSP_AT24C02_H
#define BSP_AT24C02_H

#include <stdint.h>

/* ========== 芯片参数 ========== */
#define AT24C02_I2C_ADDR        0xA0U   /* 8位写地址（7位地址0x50 << 1） */
#define AT24C02_PAGE_SIZE       8U      /* 页大小（字节） */
#define AT24C02_TOTAL_SIZE      256U    /* 总容量（字节） */
#define AT24C02_WRITE_TIMEOUT   10U     /* 写完成轮询超时（ms） */

/**
 * @brief  初始化 AT24C02 软件I2C引脚
 * @note   配置 PB8=SCL推挽输出, PB9=SDA推挽输出（通信时动态切换输入/输出）；调用前需已使能GPIOB时钟
 */
void BSP_AT24C02_Init(void);

/**
 * @brief  从指定地址读取数据（随机地址读，支持跨页连续读）
 * @param  addr   EEPROM 起始地址 (0~0xFF)
 * @param  buf    数据缓冲区
 * @param  len    读取字节数
 * @retval 0=成功, 非0=失败（I2C通信错误或地址越界）
 */
uint8_t BSP_AT24C02_Read(uint16_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  向指定地址写入数据（自动分页 + 自动等待写完成）
 * @param  addr   EEPROM 起始地址 (0~0xFF)
 * @param  buf    数据缓冲区
 * @param  len    写入字节数
 * @retval 0=成功, 非0=失败（I2C通信错误、地址越界或写超时）
 * @note   自动按8字节页边界拆分，每页写完后轮询等待内部编程完成
 */
uint8_t BSP_AT24C02_Write(uint16_t addr, const uint8_t *buf, uint32_t len);

/**
 * @brief  检查 EEPROM 是否就绪（通信测试）
 * @retval 0=就绪, 非0=无响应
 */
uint8_t BSP_AT24C02_CheckReady(void);

/**
 * @brief  总线空闲电平诊断（释放SDA/SCL为高后读取电平，判断是否被拉低/短路）
 * @retval bit0=SDA(1=高,0=低), bit1=SCL(1=高,0=低)；正常应返回 0x03
 */
uint8_t BSP_AT24C02_CheckBusLevel(void);

#endif /* BSP_AT24C02_H */
