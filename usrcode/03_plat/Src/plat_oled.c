/**
 ******************************************************************************
 * @file    plat_oled.c
 * @brief   平台抽象层（PLAT）— OLED 设备对象化封装实现
 *
 *  设备对象：
 *    name = "oled", id = 2
 *    init = BSP_OLED_Init
 ******************************************************************************
 */
#include "module_cfg.h"
#if PLAT_OLED_ENABLE

#include "plat_oled.h"
#include "bsp_oled.h"

#if PLAT_OBJ_ENABLE && PLAT_DEVMGR_ENABLE
#include "plat_mgr.h"
#endif

/* ========== 便捷显示接口 ========== */

void plat_oled_clear(void)
{
    BSP_OLED_Clear();
}

void plat_oled_set_cursor(uint8_t page, uint8_t col)
{
    BSP_OLED_SetCursor(page, col);
}

void plat_oled_show_string(uint8_t page, uint8_t col, const char *str)
{
    BSP_OLED_ShowString(page, col, str);
}

void plat_oled_show_string8x16(uint8_t page, uint8_t col, const char *str)
{
    BSP_OLED_ShowString8x16(page, col, str);
}

void plat_oled_clear_line(uint8_t page)
{
    BSP_OLED_ClearLine(page);
}

void plat_oled_fill(uint8_t data)
{
    BSP_OLED_Fill(data);
}

/* ========== 对象化设备封装 ========== */
#if PLAT_OBJ_ENABLE && PLAT_DEVMGR_ENABLE

static uint8_t oled_dev_init(void)
{
    BSP_OLED_Init();
    return 0U;
}

static const plat_ops_t s_oled_ops = {
    .init    = oled_dev_init,
    .start   = NULL,
    .process = NULL,
    .stop    = NULL,
    .deinit  = NULL,
};

plat_dev_t g_oled_dev = {
    .name  = "oled",
    .id    = 2U,
    .cfg   = NULL,
    .ctx   = NULL,
    .data  = NULL,
    .ops   = &s_oled_ops,
    .state = PLAT_STATE_NONE,
    .next  = NULL,
};

plat_dev_t *plat_oled_get_dev(void)
{
    return &g_oled_dev;
}

uint8_t plat_oled_register(void)
{
    return plat_dev_register(&g_oled_dev);
}

#endif /* PLAT_OBJ_ENABLE && PLAT_DEVMGR_ENABLE */

#endif /* PLAT_OLED_ENABLE */
