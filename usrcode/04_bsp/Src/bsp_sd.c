/**
 ******************************************************************************
 * @file    bsp_sd.c
 * @brief   SD/TF �?BSP 驱动实现（SDIO 4-bit，轮询模式）
 *
 *   实现要点�? *     - SDIO 时钟来源�?PLLQ=48MHz�? *     - 识别阶段 ClockDiv=60 -> SDIO_CK=48MHz/(2*60)=400kHz（符�?SD 规范）；
 *       识别完成�?HAL_SD_ConfigSpeed(HIGH_SPEED, ClockDiv=1) -> 24MHz�? *     - 4 位总线通过 HAL_SD_ConfigWideBusOperation 切换�? *     - 读写采用 HAL 轮询 + 有界等待，避免在 RTOS 下无限自旋�? ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_SD_ENABLE

#include "bsp_sd.h"
#include "main.h"
#if BSP_UART1_ENABLE
#include "bsp_uart.h"
#endif
#include <string.h>

/* ==========================================================================
 *  内部状�? * ========================================================================== */
static SD_HandleTypeDef          s_hsd;
static HAL_SD_CardInfoTypeDef    s_card_info;
static volatile uint8_t          s_sd_ready = 0U;

/* ==========================================================================
 *  SDIO MSP 初始化（GPIO + 时钟�? *  注：CubeMX 生成�?stm32f4xx_hal_msp.c 中目前没�?SD，故在本文件实现�? *      若将来在 CubeMX 中启�?SDIO 并重新生成，需删除本函数避免重复定义�? * ========================================================================== */
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef gpio = {0};

    (void)hsd;
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PC8..PC12 = SDIO_D0/D1/D2/D3/CK（复�?AF12，推挽，高速）
     * 板上 SDIO 无外部上�?-> 使能内部上拉（CMD/DAT 空闲高电平，提升可靠性） */
    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                     GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;   /* ԭ MEDIUM����һ�����ͱ������ʣ�400kHz ���㹻����������/�Ű������� */
    gpio.Alternate = GPIO_AF12_SDIO;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD2 = SDIO_CMD */
    gpio.Pin       = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    __HAL_RCC_SDIO_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                           GPIO_PIN_11 | GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
}

/* ==========================================================================
 *  SDIO 句柄配置（识别阶�?400kHz / 1 位）
 * ========================================================================== */
static void BSP_SD_HandleConfig(void)
{
    memset(&s_hsd, 0, sizeof(s_hsd));
    s_hsd.Instance = SDIO;
    s_hsd.Init.ClockEdge           = SDIO_CLOCK_EDGE_FALLING;  /* �?RISING：无外部上拉/长线时响应采样易错位�?CMD17 CRC 错，反相采样边沿提高建立时间余量 */
    s_hsd.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
    s_hsd.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
    s_hsd.Init.BusWide             = SDIO_BUS_WIDE_1B;   /* 识别阶段�?1 �?*/
    s_hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    s_hsd.Init.ClockDiv            = 60;                 /* 48MHz/120 = 400kHz */
}

/* ==========================================================================
 *  初始�? * ========================================================================== */
uint8_t BSP_SD_Init(void)
{
    uint8_t attempt;

    s_sd_ready = 0U;

    BSP_SD_HandleConfig();

    /* 上电识别：最多重�?3 次（首次可能因卡上电时序失败�?*/
    for (attempt = 0U; attempt < 3U; attempt++) {
        if (HAL_SD_Init(&s_hsd) == HAL_OK) {
            break;
        }
        HAL_Delay(200U);
    }
    if (attempt >= 3U) {
        return 1U;   /* 识别失败（无�?/ 卡坏 / 接线问题�?*/
    }

    /* 保持 1 位总线 + 400kHz（不提速）�?     * 识别/读容量在 400kHz 下必然成功，12MHz/24MHz 提速在本板无外部上拉时
     * 数据读取不稳定（fr=1）�?00kHz 1-bit �?50KB/s，对 5s 一行的日志足够�?     * 若需提速，先解决上�?线序再改 ClockDiv�?     * 注意：HAL_SD_Init 已按 BSP_SD_HandleConfig(1-bit, ClockDiv=60) 完成配置�?     * 此处不再重复 SDIO_Init（s_hsd.Init.PowerState 未设�?OFF，重复调用会�?     * SDIO POWER 寄存器写�?OFF，干扰外�?卡状态导致传输命�?CRC 错乱）�?*/
    HAL_SD_GetCardInfo(&s_hsd, &s_card_info);
    s_sd_ready = 1U;
    return 0U;
}

uint8_t BSP_SD_DeInit(void)
{
    HAL_SD_DeInit(&s_hsd);
    s_sd_ready = 0U;
    return 0U;
}

/* ==========================================================================
 *  扇区读写（轮�?+ 有界等待�? * ========================================================================== */
uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t BlockAdd,
                          uint32_t NumberOfBlocks, uint32_t Timeout)
{
    uint32_t t0;
    uint8_t  attempt;
    static uint16_t s_fail_cnt = 0U;

    if ((s_sd_ready == 0U) || (pData == NULL)) {
        return 1U;
    }
    /* Intermittent link errors (RX FIFO overrun etc.): retry once; a same-sector retry usually succeeds */
    for (attempt = 0U; attempt < 2U; attempt++) {
        if (HAL_SD_ReadBlocks(&s_hsd, (uint8_t *)pData, BlockAdd, NumberOfBlocks, Timeout) == HAL_OK) {
            break;
        }
    }
    if (attempt >= 2U) {
        s_fail_cnt++;
#if BSP_UART1_ENABLE
        /* Throttle failure prints (1st, 10th, 20th...) to avoid blocking UART starving FIFO drain */
        if ((s_fail_cnt == 1U) || ((s_fail_cnt % 10U) == 0U)) {
            BSP_UART1_Printf("[SD] rd blk=%lu err=%lx sta=%lx nbr=%lu\r\n",
                             (unsigned long)BlockAdd, (unsigned long)s_hsd.ErrorCode,
                             (unsigned long)s_hsd.Instance->STA,
                             (unsigned long)s_hsd.SdCard.LogBlockNbr);
        }
#endif
        return 1U;
    }
    s_fail_cnt = 0U;
    t0 = HAL_GetTick();
    t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&s_hsd) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - t0) > Timeout) {
            return 1U;
        }
    }
    return 0U;
}

uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t BlockAdd,
                           uint32_t NumberOfBlocks, uint32_t Timeout)
{
    uint32_t t0;
    uint8_t  attempt;

    if ((s_sd_ready == 0U) || (pData == NULL)) {
        return 1U;
    }
    /* Intermittent link errors: retry once before giving up */
    for (attempt = 0U; attempt < 2U; attempt++) {
        if (HAL_SD_WriteBlocks(&s_hsd, (uint8_t *)pData, BlockAdd, NumberOfBlocks, Timeout) == HAL_OK) {
            break;
        }
    }
    if (attempt >= 2U) {
        return 1U;
    }
    t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&s_hsd) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - t0) > Timeout) {
            return 1U;
        }
    }
    return 0U;
}

uint8_t BSP_SD_Sync(void)
{
    uint32_t t0;

    if (s_sd_ready == 0U) {
        return 1U;
    }
    t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&s_hsd) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - t0) > 10000U) {
            return 1U;
        }
    }
    return 0U;
}

/* ==========================================================================
 *  状态与信息
 * ========================================================================== */
uint8_t BSP_SD_IsReady(void)
{
    return s_sd_ready;
}

uint8_t BSP_SD_GetCardInfo(BSP_SD_CardInfo_t *info)
{
    if ((info == NULL) || (s_sd_ready == 0U)) {
        return 1U;
    }
    info->block_count    = s_card_info.BlockNbr;
    info->block_size     = s_card_info.BlockSize;
    info->capacity_bytes = (uint64_t)s_card_info.BlockNbr * s_card_info.BlockSize;
    info->card_type      = (s_card_info.CardType == CARD_SDHC_SDXC) ? 2U : 1U;
    return 0U;
}

/* ==========================================================================
 *  SDIO 中断处理（必须实现！HAL_SD_ReadBlocks/WriteBlocks 会主动使�? *  DCRCFAIL/DTIMEOUT 等错误中断，缺此 ISR 时中断会掉进 Default_Handler
 *  死循环，看门狗饿死反复复位）
 * ========================================================================== */
void SDIO_IRQHandler(void)
{
    HAL_SD_IRQHandler(&s_hsd);
}

#endif /* BSP_SD_ENABLE */
