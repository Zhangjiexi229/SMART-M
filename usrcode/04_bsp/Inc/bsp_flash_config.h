/**
 ******************************************************************************
 * @file    bsp_flash_config.h
 * @brief   STM32F407 内部Flash配置存储驱动头文件
 *
 *  存储区域（芯片 STM32F407VET6，512KB Flash，代码约占 0x08000000~0x08022500）：
 *    阈值配置   Sector 6   @ 0x08040000   （最后 128KB 区间，低风险空闲区）
 *    网络配置   Sector 7   @ 0x08060000   （最后 128KB 区间，低风险空闲区）
 *
 *  数据布局（每个存储块相同）：
 *    +--------+------------------+----------+
 *    | magic  | data[N]          | checksum |
 *    +--------+------------------+----------+
 *    4B       <=N                4B
 *    magic 区分用途；checksum=对 magic+data 的逐字节累加
 *
 *  ▍Flash 写入注意（STM32F4 系列）：
 *    1. 写前必须先擦除，擦除按扇区进行（128KB 扇区擦除约 1~2s，期间 CPU 阻塞）
 *    2. 写前解锁 HAL_FLASH_Unlock()，写后锁定 HAL_FLASH_Lock()
 *    3. Flash 寿命约 1 万次擦写，只在用户确认保存时调用，勿频繁写入
 *    4. 写 Flash 期间 CPU 暂停执行（Flash 忙），避免在中断/实时要求高的
 *       路径调用；调用方应在保存前喂一次看门狗（128KB 扇区擦除可能接近
 *       IWDG 3s 超时窗口）
 ******************************************************************************
 */
#ifndef BSP_FLASH_CONFIG_H
#define BSP_FLASH_CONFIG_H

#include <stdint.h>

/* ========== 阈值配置存储参数 ========== */
#define BSP_FLASH_CFG_SECTOR      FLASH_SECTOR_6
#define BSP_FLASH_CFG_ADDR        (0x08040000UL)
#define BSP_FLASH_CFG_MAGIC       0x4D4F544FUL   /* "MOTO" */
#define BSP_FLASH_CFG_DATA_SIZE   64U            /* 配置数据块最大长度（字节） */

/* ========== 网络配置存储参数（WiFi + MQTT 云，app_netcfg 使用） ========== */
#define BSP_FLASH_NET_SECTOR      FLASH_SECTOR_7
#define BSP_FLASH_NET_ADDR        (0x08060000UL)
#define BSP_FLASH_NET_MAGIC       0x4E455443UL   /* "NETC" */
#define BSP_FLASH_NET_DATA_SIZE   512U           /* 网络配置数据块最大长度（字节） */

/* 完整存储块布局（阈值配置） */
typedef struct {
    uint32_t magic;                       /* 有效性标识 */
    uint8_t  data[BSP_FLASH_CFG_DATA_SIZE]; /* 配置数据块 */
    uint32_t checksum;                    /* magic+data 逐字节累加和 */
} BSP_Flash_CfgBlock_t;

/* 完整存储块布局（网络配置，data 较大，单独定义） */
typedef struct {
    uint32_t magic;                       /* 有效性标识 */
    uint8_t  data[BSP_FLASH_NET_DATA_SIZE]; /* 配置数据块 */
    uint32_t checksum;                    /* magic+data 逐字节累加和 */
} BSP_Flash_NetCfgBlock_t;

/**
 * @brief  保存配置数据到内部Flash（擦除Sector6+按字编程）
 * @param  data  配置数据缓冲区
 * @param  len   数据长度（<= BSP_FLASH_CFG_DATA_SIZE）
 * @retval 0=成功，1=长度超限，2=擦除失败，3=编程失败
 * @note   低频调用（用户确认保存）；调用期间Flash忙，CPU阻塞约1~2s
 */
uint8_t BSP_FLASH_SaveConfig(const uint8_t *data, uint32_t len);

/**
 * @brief  从内部Flash加载配置数据（校验magic与checksum）
 * @param  data  输出缓冲区
 * @param  len   期望数据长度（<= BSP_FLASH_CFG_DATA_SIZE）
 * @retval 0=成功，1=无有效配置(magic不匹配)，2=校验失败，3=长度超限
 */
uint8_t BSP_FLASH_LoadConfig(uint8_t *data, uint32_t len);

/**
 * @brief  保存网络配置到内部Flash（擦除Sector7+按字编程）
 * @param  data  配置数据缓冲区
 * @param  len   数据长度（<= BSP_FLASH_NET_DATA_SIZE）
 * @retval 0=成功，1=长度超限，2=擦除失败，3=编程失败
 * @note   低频调用（小程序 SETCFG 保存）；调用期间Flash忙，CPU阻塞约1~2s
 */
uint8_t BSP_FLASH_SaveNetConfig(const uint8_t *data, uint32_t len);

/**
 * @brief  从内部Flash加载网络配置（校验magic与checksum）
 * @param  data  输出缓冲区
 * @param  len   期望数据长度（<= BSP_FLASH_NET_DATA_SIZE）
 * @retval 0=成功，1=无有效配置(magic不匹配)，2=校验失败，3=长度超限
 */
uint8_t BSP_FLASH_LoadNetConfig(uint8_t *data, uint32_t len);

#endif /* BSP_FLASH_CONFIG_H */
