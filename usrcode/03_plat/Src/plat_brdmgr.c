/**
 ******************************************************************************
 * @file    plat_brdmgr.c
 * @brief   平台抽象层（PLAT）— 板级管理器实现（board manager）
 *
 *  功能：
 *    - 将板级设备表 g_plat_brd 中的全部设备批量注册到 device manager
 *    - 提供按名取设备（依赖注入入口）与引脚映射查询
 ******************************************************************************
 */
#include "module_cfg.h"
#if PLAT_BRDMGR_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE

#include "plat_brd.h"
#include <string.h>

uint8_t plat_brdmgr_init(void)
{
    uint16_t i;

    if (g_plat_brd.dev_count > PLAT_BRD_MAX_DEV) {
        return 0xFFU;   /* 板级表超限 */
    }
    for (i = 0U; i < g_plat_brd.dev_count; i++) {
        if (g_plat_brd.devs[i] == NULL) continue;
        if (plat_devmgr_register(g_plat_brd.devs[i]) != 0U) {
            return (uint8_t)(1U + i);   /* 返回首个失败位置（1-based） */
        }
    }
    return 0U;
}

plat_dev_t *plat_brdmgr_get(const char *name)
{
    return plat_devmgr_find(name);
}

const plat_pinmap_t *plat_brdmgr_find_pinmap(const char *dev_name)
{
    uint16_t i;

    if (dev_name == NULL) return NULL;
    for (i = 0U; i < g_plat_pinmap_count; i++) {
        if (strcmp(g_plat_pinmap[i].dev_name, dev_name) == 0) {
            return &g_plat_pinmap[i];
        }
    }
    return NULL;
}

#endif /* PLAT_BRDMGR_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE */
