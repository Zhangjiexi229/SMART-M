/**
 ******************************************************************************
 * @file    bsp_flash_config.c
 * @brief   STM32F407 内部Flash配置存储驱动实现 — Sector11 @ 0x080E0000
 *
 *  流程：
 *    保存：填magic -> 填数据 -> 算checksum -> 解锁 -> 擦扇区 -> 按字编程 -> 锁定
 *    加载：读地址 -> 验magic -> 重算checksum比对 -> 输出数据
 *
 *  依赖：STM32 HAL Flash 库（stm32f4xx_hal_flash.h / _ex.h）
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_FLASH_CONFIG_ENABLE

#include "bsp_flash_config.h"
#include "main.h"
#include <stddef.h>

/* 校验和：对块内 magic+data（不含checksum字段）逐字节累加 */
static uint32_t BSP_FLASH_CalcChecksum(const BSP_Flash_CfgBlock_t *block)
{
    const uint8_t *p = (const uint8_t *)block;
    uint32_t len = (uint32_t)sizeof(BSP_Flash_CfgBlock_t) - 4U;   /* 不含checksum */
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < len; i++) {
        sum += p[i];
    }
    return sum;
}

uint8_t BSP_FLASH_SaveConfig(const uint8_t *data, uint32_t len)
{
    BSP_Flash_CfgBlock_t block;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0U;
    uint32_t *src;
    uint32_t addr;
    uint32_t i;

    if ((data == NULL) || (len == 0U) || (len > BSP_FLASH_CFG_DATA_SIZE)) {
        return 1U;
    }

    /* 1. 组装存储块 */
    block.magic = BSP_FLASH_CFG_MAGIC;
    for (i = 0U; i < len; i++) {
        block.data[i] = data[i];
    }
    for (; i < BSP_FLASH_CFG_DATA_SIZE; i++) {
        block.data[i] = 0U;
    }
    block.checksum = BSP_FLASH_CalcChecksum(&block);

    /* 2. 解锁Flash */
    HAL_FLASH_Unlock();

    /* 3. 擦除Sector11 */
    erase_init.TypeErase   = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector      = BSP_FLASH_CFG_SECTOR;
    erase_init.NbSectors   = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return 2U;
    }

    /* 4. 按字编程（4字节对齐写入） */
    src  = (uint32_t *)&block;
    addr = BSP_FLASH_CFG_ADDR;
    for (i = 0U; i < (sizeof(BSP_Flash_CfgBlock_t) / 4U); i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return 3U;
        }
        addr += 4U;
    }

    /* 5. 锁定Flash */
    HAL_FLASH_Lock();

    return 0U;
}

uint8_t BSP_FLASH_LoadConfig(uint8_t *data, uint32_t len)
{
    const BSP_Flash_CfgBlock_t *block;
    uint32_t i;

    if ((data == NULL) || (len == 0U) || (len > BSP_FLASH_CFG_DATA_SIZE)) {
        return 3U;
    }

    block = (const BSP_Flash_CfgBlock_t *)BSP_FLASH_CFG_ADDR;

    /* 1. 校验magic */
    if (block->magic != BSP_FLASH_CFG_MAGIC) {
        return 1U;   /* 无有效配置（首次上电） */
    }

    /* 2. 校验checksum */
    if (BSP_FLASH_CalcChecksum(block) != block->checksum) {
        return 2U;
    }

    /* 3. 输出数据 */
    for (i = 0U; i < len; i++) {
        data[i] = block->data[i];
    }
    return 0U;
}

#endif /* BSP_FLASH_CONFIG_ENABLE */
