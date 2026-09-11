/**
 ******************************************************************************
 * @file    bsp_flash_config.h
 * @brief   STM32F407 内部Flash配置存储驱动头文件
 *
 *  存储区域（Sector 11，最后 128KB，代码从 0x08000000 开始一般不占用）：
 *    起始地址 0x080E0000，1MB Flash 的最后一个扇区
 *
 *  数据布局：
 *    +--------+------------------+----------+
 *    | magic  | data[64]         | checksum |
 *    +--------+------------------+----------+
 *    4B       <=64B              4B
 *    magic=0x4D4F544F("MOTO") 校验=对 magic+data 的逐字节累加
 *
 *  ▍Flash 写入注意（STM32F4 系列）：
 *    1. 写前必须先擦除，擦除按扇区进行（本驱动固定擦 Sector11）
 *    2. 写前解锁 HAL_FLASH_Unlock()，写后锁定 HAL_FLASH_Lock()
 *    3. Flash 寿命约 1 万次擦写，只在用户确认保存时调用，勿频繁写入
 *    4. 写 Flash 期间 CPU 暂停执行（Flash 忙），避免在中断/实时要求高的
 *       路径调用；本驱动设计为"按键确认保存"低频场景使用
 ******************************************************************************
 */
#ifndef BSP_FLASH_CONFIG_H
#define BSP_FLASH_CONFIG_H

#include <stdint.h>

/* ========== 存储参数 ========== */
#define BSP_FLASH_CFG_SECTOR      FLASH_SECTOR_11
#define BSP_FLASH_CFG_ADDR        (0x080E0000UL)
#define BSP_FLASH_CFG_MAGIC       0x4D4F544FUL   /* "MOTO" */
#define BSP_FLASH_CFG_DATA_SIZE   64U            /* 配置数据块最大长度（字节） */

/* 完整存储块布局 */
typedef struct {
    uint32_t magic;                       /* 有效性标识 */
    uint8_t  data[BSP_FLASH_CFG_DATA_SIZE]; /* 配置数据块 */
    uint32_t checksum;                    /* magic+data 逐字节累加和 */
} BSP_Flash_CfgBlock_t;

/**
 * @brief  保存配置数据到内部Flash（擦除Sector11+按字编程）
 * @param  data  配置数据缓冲区
 * @param  len   数据长度（<= BSP_FLASH_CFG_DATA_SIZE）
 * @retval 0=成功，1=长度超限，2=擦除失败，3=编程失败
 * @note   低频调用（按键确认保存）；调用期间Flash忙，CPU阻塞数ms
 */
uint8_t BSP_FLASH_SaveConfig(const uint8_t *data, uint32_t len);

/**
 * @brief  从内部Flash加载配置数据（校验magic与checksum）
 * @param  data  输出缓冲区
 * @param  len   期望数据长度（<= BSP_FLASH_CFG_DATA_SIZE）
 * @retval 0=成功，1=无有效配置(magic不匹配)，2=校验失败，3=长度超限
 */
uint8_t BSP_FLASH_LoadConfig(uint8_t *data, uint32_t len);

#endif /* BSP_FLASH_CONFIG_H */
