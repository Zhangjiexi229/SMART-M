/**
 ******************************************************************************
 * @file    app_config.c
 * @brief   告警阈值配置实现 — 默认值 + 内部Flash加载/保存
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_CONFIG_ENABLE

#include "app_config.h"
#include "bsp_uart.h"
#if BSP_FLASH_CONFIG_ENABLE
#include "bsp_flash_config.h"
#endif

/* ==========================================================================
 *  全局配置与状态
 * ========================================================================== */
ThresholdConfig_t g_threshold = { 60.0f, 3.0f, 5.0f };
SetMode_t         g_set_mode   = SET_MODE_EXIT;
uint8_t           g_set_selected = 0U;

/* 默认阈值（首次上电无有效配置时使用） */
#define APP_CONFIG_DEFAULT_TEMP_HIGH   30.0f
#define APP_CONFIG_DEFAULT_VIB_HIGH     3.0f
#define APP_CONFIG_DEFAULT_CURR_HIGH    5.0f

/* ==========================================================================
 *  API
 * ========================================================================== */
void APP_CONFIG_Init(void)
{
    /* 默认值 */
    g_threshold.temp_high = APP_CONFIG_DEFAULT_TEMP_HIGH;
    g_threshold.vib_high  = APP_CONFIG_DEFAULT_VIB_HIGH;
    g_threshold.curr_high = APP_CONFIG_DEFAULT_CURR_HIGH;
    g_set_mode   = SET_MODE_EXIT;
    g_set_selected = 0U;

#if BSP_FLASH_CONFIG_ENABLE
    /* 从内部Flash加载（失败则保持默认） */
    if (BSP_FLASH_LoadConfig((uint8_t *)&g_threshold, sizeof(g_threshold)) == 0U) {
        BSP_UART1_Printf("[CFG] Threshold loaded from Flash: T=%.1f V=%.1f C=%.1f\r\n",
                         (double)g_threshold.temp_high,
                         (double)g_threshold.vib_high,
                         (double)g_threshold.curr_high);
    } else {
        BSP_UART1_Printf("[CFG] No valid threshold in Flash, using defaults\r\n");
    }
#else
    BSP_UART1_Printf("[CFG] Flash config disabled, using defaults\r\n");
#endif
}

uint8_t APP_CONFIG_Save(void)
{
#if BSP_FLASH_CONFIG_ENABLE
    uint8_t ret = BSP_FLASH_SaveConfig((const uint8_t *)&g_threshold, sizeof(g_threshold));
    if (ret == 0U) {
        BSP_UART1_Printf("[CFG] Threshold saved: T=%.1f V=%.1f C=%.1f\r\n",
                         (double)g_threshold.temp_high,
                         (double)g_threshold.vib_high,
                         (double)g_threshold.curr_high);
    } else {
        BSP_UART1_Printf("[CFG] Threshold save FAILED (code=%u)\r\n", (unsigned)ret);
    }
    return ret;
#else
    BSP_UART1_Printf("[CFG] Flash config disabled, threshold not saved\r\n");
    return 1U;
#endif
}

#endif /* APP_CONFIG_ENABLE */
