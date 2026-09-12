/**
 ******************************************************************************
 * @file    bsp_i2c_soft.h
 * @brief   共享软件I2C总线驱动头文件 — PB8=SCL, PB9=SDA（与板载IIC排针一致）
 *
 *  挂载器件（7位I2C地址，互不冲突）：
 *    AT24C02  = 0x50  板载EEPROM（bsp_at24c02 自带软件I2C，不依赖本驱动）
 *    SHT30    = 0x44  温湿度传感器（ADDR引脚接地）
 *    QMI8658  = 0x6B  六轴IMU（本模块AD0板上固定接地→0x6B）
 *    INA226   = 0x40  电源监测（A0/A1接地）
 *    MPU6050  = 0x68  板载IMU（本工程未使用）
 *
 *  设计原则：
 *    - 每个 I2C 事务（Start...Stop）用关中断临界区保护，防止 FreeRTOS 任务
 *      抢占导致总线时序被破坏；SHT30 的测量等待（~20ms）在临界区外进行
 *    - 与 bsp_at24c02 共用 PB8/PB9，双方初始化互相覆盖为相同配置，无冲突
 *    - 本驱动是只做"寄存器读写"的通用主机，具体器件语义在各 bsp_xxx 中实现
 ******************************************************************************
 */
#ifndef BSP_I2C_SOFT_H
#define BSP_I2C_SOFT_H

#include <stdint.h>

/* ========== 总线引脚映射 ========== */
#define I2C_SOFT_SCL_GPIO_Port   GPIOB
#define I2C_SOFT_SCL_Pin         GPIO_PIN_8
#define I2C_SOFT_SDA_GPIO_Port   GPIOB
#define I2C_SOFT_SDA_Pin         GPIO_PIN_9

/**
 * @brief  软件I2C总线初始化（PB8=SCL推挽输出, PB9=SDA动态切换输入/输出）
 * @note   在任务调度器启动前调用（APP_Init）；各器件 BSP_xxx_Init 前必须先调用本函数
 */
void BSP_I2C_Soft_Init(void);

/**
 * @brief  探测指定7位地址的器件是否在线
 * @param  dev_addr  器件7位I2C地址（如 0x44）
 * @retval 0=在线（收到ACK），非0=无应答
 */
uint8_t BSP_I2C_Soft_Probe(uint8_t dev_addr);

/**
 * @brief  I2C总线恢复 — 手动产生9个SCL时钟脉冲 + 停止位，释放被拉低的SDA
 * @note   多个器件共用I2C总线时，某从机异常拉低SDA不释放会导致总线死锁，
 *         调用本函数后总线恢复空闲，随后重新初始化SCL/SDA为推挽/输入模式。
 *         由应用层在"连续读取失败N次"时触发（见 app_sht30/app_qmi8658/
 *         app_ina226 的失败计数逻辑）。
 */
void BSP_I2C_Soft_Recover(void);

/**
 * @brief  向器件指定寄存器写入多字节数据
 * @param  dev_addr  器件7位I2C地址
 * @param  reg       目标寄存器地址
 * @param  buf       数据缓冲区
 * @param  len       写入字节数
 * @retval 0=成功，非0=失败（NACK）
 */
uint8_t BSP_I2C_Soft_WriteRegs(uint8_t dev_addr, uint8_t reg,
                               const uint8_t *buf, uint32_t len);

/**
 * @brief  向器件指定寄存器写入1字节
 * @param  dev_addr  器件7位I2C地址
 * @param  reg       目标寄存器地址
 * @param  data      待写入字节
 * @retval 0=成功，非0=失败
 */
uint8_t BSP_I2C_Soft_WriteReg(uint8_t dev_addr, uint8_t reg, uint8_t data);

/**
 * @brief  从器件指定寄存器开始连续读取多字节
 * @param  dev_addr  器件7位I2C地址
 * @param  reg       起始寄存器地址
 * @param  buf       数据缓冲区
 * @param  len       读取字节数
 * @retval 0=成功，非0=失败（NACK或参数错误）
 */
uint8_t BSP_I2C_Soft_ReadRegs(uint8_t dev_addr, uint8_t reg,
                              uint8_t *buf, uint32_t len);

/**
 * @brief  从器件"裸读"多字节（无寄存器地址，SHT30 单次测量专用）
 * @param  dev_addr  器件7位I2C地址
 * @param  buf       数据缓冲区
 * @param  len       读取字节数
 * @retval 0=成功，非0=失败（NACK或参数错误）
 * @note   SHT30 官方读时序：发测量命令 → 等待 → 重复起始+读方向 → 直接读6字节，
 *         中间不需要写寄存器地址。此前用 ReadRegs(addr,0x00,...) 会多写一个
 *         0x00 字节（SHT30 忽略未定义命令），现改用本函数严格对齐官方时序。
 */
uint8_t BSP_I2C_Soft_Read(uint8_t dev_addr, uint8_t *buf, uint32_t len);

#endif /* BSP_I2C_SOFT_H */
