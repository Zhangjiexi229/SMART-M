/**
 ******************************************************************************
 * @file    bsp_sd.h
 * @brief   SD/TF 卡 BSP 驱动（SDIO 4-bit）— 硬件底层读写
 *
 *   硬件连接（STM32F407VET6 核心板 U6 MiniSD 插座）：
 *     PC8  - SDIO_D0
 *     PC9  - SDIO_D1
 *     PC10 - SDIO_D2
 *     PC11 - SDIO_D3
 *     PC12 - SDIO_CK
 *     PD2  - SDIO_CMD
 *     3V3/GND + 0.1uF 去耦（板上 C12）
 *
 *   注意：
 *     - 本模块自包含 GPIO/时钟/SDIO 初始化，不依赖 CubeMX 重新生成；
 *     - 若在 CubeMX 中手工启用 SDIO 并重新生成，请删除本文件中
 *       HAL_SD_MspInit/HAL_SD_MspDeInit 与 stm32f4xx_hal_msp.c 中重复定义；
 *     - 板上 SDIO 信号无外部上拉电阻，本驱动对 CMD/D0-D3 使能了内部上拉，
 *       长期可靠性建议在 3V3 上加 10K 上拉（见《SD卡数据存储模块说明.md》）。
 ******************************************************************************
 */
#ifndef BSP_SD_H
#define BSP_SD_H

#include <stdint.h>

/* ==========================================================================
 *  卡信息
 * ========================================================================== */
typedef struct {
    uint32_t block_count;     /*!< 总扇区数（512B/扇区） */
    uint32_t block_size;      /*!< 扇区大小（固定 512） */
    uint64_t capacity_bytes;  /*!< 容量（字节，64 位防大容量卡溢出） */
    uint8_t  card_type;       /*!< 0=无卡, 1=SDSC(v1), 2=SDHC(v2), 3=SDXC(v3) */
} BSP_SD_CardInfo_t;

/* ==========================================================================
 *  API
 * ========================================================================== */

/**
 * @brief  初始化 SDIO + SD 卡（含上电识别、切 4 位总线、切高速 24MHz）
 * @retval 0 成功；非 0 失败（1=识别失败, 2=切4位失败）
 */
uint8_t BSP_SD_Init(void);

/**
 * @brief  反初始化 SDIO
 * @retval 0 成功
 */
uint8_t BSP_SD_DeInit(void);

/**
 * @brief  读扇区（多扇区）
 * @param  pData          数据缓冲（建议 4 字节对齐；未对齐由 diskio 中转）
 * @param  BlockAdd       起始扇区 LBA
 * @param  NumberOfBlocks 扇区数
 * @param  Timeout        超时（ms）
 * @retval 0 成功；非 0 失败
 */
uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t BlockAdd,
                          uint32_t NumberOfBlocks, uint32_t Timeout);

/**
 * @brief  写扇区（多扇区）
 * @retval 0 成功；非 0 失败
 */
uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t BlockAdd,
                           uint32_t NumberOfBlocks, uint32_t Timeout);

/**
 * @brief  等待卡进入传输状态（写同步）
 * @retval 0 成功；非 0 失败
 */
uint8_t BSP_SD_Sync(void);

/**
 * @brief  查询卡是否已初始化就绪
 * @retval 1 就绪；0 未就绪
 */
uint8_t BSP_SD_IsReady(void);

/**
 * @brief  获取卡信息
 * @param  info 输出卡信息（不可为 NULL）
 * @retval 0 成功；非 0 失败
 */
uint8_t BSP_SD_GetCardInfo(BSP_SD_CardInfo_t *info);

#endif /* BSP_SD_H */
