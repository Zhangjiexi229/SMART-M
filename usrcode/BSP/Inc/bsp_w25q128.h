/**
 ******************************************************************************
 * @file    bsp_w25q128.h
 * @brief   W25Q128 SPI Flash 板级驱动头文件
 *
 *  引脚分配 (在 main.h 中定义):
 *    PB3  = SPI_SCLK_Pin  (SPI_SCLK_GPIO_Port = GPIOB)
 *    PB5  = SPI_MOSI_Pin (SPI_MOSI_GPIO_Port = GPIOB)
 *    PB4  = SPI_MISO_Pin (SPI_MISO_GPIO_Port = GPIOB)
 *    PG10 = SPI_NSS_Pin  (SPI_NSS_GPIO_Port = GPIOG)
 *
 *  功能:
 *    - JEDEC ID / Device ID 读取
 *    - 状态寄存器读写
 *    - 写使能/禁用
 *    - 扇区(4KB)/块(32KB/64KB)/全片擦除
 *    - 页编程(256B) + 跨页自动分片写入
 *    - 任意地址连续读取
 *    - 综合自检 SelfTest
 ******************************************************************************
 */
#ifndef BSP_W25Q128_H
#define BSP_W25Q128_H

#if BSP_W25Q128_ENABLE

#include <stdint.h>
#include "main.h"

/* ==================== JEDEC ID (W25Q128) ==================== */
#define W25Q128_JEDEC_MANUFACTURER   0xEFU   /* Winbond */
#define W25Q128_JEDEC_MEMORY_TYPE    0x40U
#define W25Q128_JEDEC_CAPACITY       0x18U   /* 128Mbit = 16MB */

/* ==================== 状态寄存器位定义 ==================== */
#define W25Q128_SR1_BUSY              0x01U   /* 擦除/写入忙 */
#define W25Q128_SR1_WEL               0x02U   /* 写使能锁存 */
#define W25Q128_SR1_BP0               0x04U
#define W25Q128_SR1_BP1               0x08U
#define W25Q128_SR1_BP2               0x10U
#define W25Q128_SR1_TB                0x20U
#define W25Q128_SR1_SEC               0x40U
#define W25Q128_SR1_SRP0              0x80U

#define W25Q128_SR2_SRP1              0x01U
#define W25Q128_SR2_QE                0x02U
#define W25Q128_SR2_LB1               0x08U
#define W25Q128_SR2_LB2               0x10U
#define W25Q128_SR2_LB3               0x20U
#define W25Q128_SR2_CMP               0x40U
#define W25Q128_SR2_SUS               0x80U

/* ==================== 容量参数 ==================== */
#define W25Q128_PAGE_SIZE             256U
#define W25Q128_SECTOR_SIZE           4096U
#define W25Q128_BLOCK32_SIZE          (32U * 1024U)
#define W25Q128_BLOCK64_SIZE          (64U * 1024U)
#define W25Q128_CHIP_SIZE             (16U * 1024U * 1024U)

/* ==================== API ==================== */

/**
 * @brief  初始化 W25Q128 GPIO (软件 SPI)
 * @return 0=成功
 */
uint8_t BSP_W25Q128_Init(void);

/**
 * @brief  读取 JEDEC ID (3字节: 厂商/类型/容量)
 */
uint8_t BSP_W25Q128_ReadJEDEC_ID(uint8_t *manuf, uint8_t *type, uint8_t *cap);

/**
 * @brief  读取 Device ID (1字节)
 */
uint8_t BSP_W25Q128_ReadDeviceID(uint8_t *dev_id);

/**
 * @brief  读取状态寄存器1
 */
uint8_t BSP_W25Q128_ReadStatus1(void);

/**
 * @brief  读取状态寄存器2
 */
uint8_t BSP_W25Q128_ReadStatus2(void);

/**
 * @brief  查询 BUSY 位
 * @return 1=忙, 0=空闲
 */
uint8_t BSP_W25Q128_IsBusy(void);

/**
 * @brief  等待 BUSY 清零
 * @param  timeout_ms 超时(毫秒)
 * @return 0=成功, 1=超时
 */
uint8_t BSP_W25Q128_WaitBusy(uint32_t timeout_ms);

/**
 * @brief  写使能 (WEL=1)
 */
void BSP_W25Q128_WriteEnable(void);

/**
 * @brief  写禁用 (WEL=0)
 */
void BSP_W25Q128_WriteDisable(void);

/**
 * @brief  扇区擦除 (4KB)
 * @param  addr 扇区内任意地址
 * @return 0=成功, 1=超时
 */
uint8_t BSP_W25Q128_SectorErase(uint32_t addr);

/**
 * @brief  32KB 块擦除
 */
uint8_t BSP_W25Q128_Block32Erase(uint32_t addr);

/**
 * @brief  64KB 块擦除
 */
uint8_t BSP_W25Q128_Block64Erase(uint32_t addr);

/**
 * @brief  全片擦除 (约30秒)
 */
uint8_t BSP_W25Q128_ChipErase(void);

/**
 * @brief  从指定地址读取数据 (Read Data, 0x03)
 * @param  addr 24位起始地址
 * @param  buf  目的缓冲区
 * @param  len  字节数
 * @return 0=成功
 */
uint8_t BSP_W25Q128_Read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  写入数据到指定地址 (自动跨页分片)
 * @note   写入前必须确保目标区域已擦除(0xFF)
 * @param  addr 24位起始地址
 * @param  buf  源数据
 * @param  len  字节数
 * @return 0=成功, 1=失败
 */
uint8_t BSP_W25Q128_Write(uint32_t addr, const uint8_t *buf, uint32_t len);

/**
 * @brief  检查指定区域是否已擦除(全0xFF)
 * @return 1=已擦除, 0=未擦除
 */
uint8_t BSP_W25Q128_IsErased(uint32_t addr, uint32_t len);

/**
 * @brief  综合自检: 读ID -> 读SR -> 擦除扇区0 -> 验证 -> 写32B -> 回读比对
 * @note   通过 BSP_UART1_Printf 输出结果, 会修改扇区0数据!
 * @return 0=全部通过, 非0=失败位掩码
 */
uint8_t BSP_W25Q128_SelfTest(void);

#endif /* BSP_W25Q128_ENABLE */

#endif /* BSP_W25Q128_H */
