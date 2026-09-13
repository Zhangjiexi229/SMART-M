/**
 ******************************************************************************
 * @file    plat_devmgr.c
 * @brief   平台抽象层（PLAT）— 设备管理器实现
 *
 *  功能：
 *    - 设备注册/注销/按名查找（单向链表）
 *    - 单设备生命周期：init / start / process / stop / deinit
 *    - 批量驱动：init_all / start_all / process_all / stop_all / deinit_all
 *  状态机：
 *    REGISTERED → INITED → STARTED ⇄ STOPPED → DEINITED
 ******************************************************************************
 */
#include "module_cfg.h"
#if PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE

#include "plat_mgr.h"
#include <string.h>

static plat_dev_t *s_dev_head = NULL;
static uint16_t s_dev_count = 0U;

/* ========== 注册 / 注销 / 查找 ========== */

uint8_t plat_devmgr_register(plat_dev_t *dev)
{
    if ((dev == NULL) || (dev->name == NULL)) {
        return 1U;   /* 参数错误 */
    }
    if (plat_devmgr_find(dev->name) != NULL) {
        return 2U;   /* 重名，拒绝注册 */
    }
    dev->next  = s_dev_head;
    dev->state = PLAT_STATE_REGISTERED;
    s_dev_head = dev;
    s_dev_count++;
    return 0U;
}

uint8_t plat_devmgr_unregister(plat_dev_t *dev)
{
    plat_dev_t **pp = &s_dev_head;

    if (dev == NULL) return 1U;
    while (*pp != NULL) {
        if (*pp == dev) {
            *pp = dev->next;
            dev->next  = NULL;
            dev->state = PLAT_STATE_NONE;
            if (s_dev_count > 0U) s_dev_count--;
            return 0U;
        }
        pp = &(*pp)->next;
    }
    return 1U;   /* 未注册 */
}

plat_dev_t *plat_devmgr_find(const char *name)
{
    plat_dev_t *p = s_dev_head;

    if (name == NULL) return NULL;
    while (p != NULL) {
        if ((p->name != NULL) && (strcmp(p->name, name) == 0)) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

uint16_t plat_devmgr_count(void)
{
    return s_dev_count;
}

/* ========== 单设备生命周期原语 ========== */

uint8_t plat_dev_register(plat_dev_t *dev)
{
    return plat_devmgr_register(dev);
}

uint8_t plat_dev_init(plat_dev_t *dev)
{
    if ((dev == NULL) || (dev->ops == NULL)) return 1U;
    if (dev->state != PLAT_STATE_REGISTERED) return 2U;   /* 状态不允许 */
    if ((dev->ops->init != NULL) && (dev->ops->init() != 0U)) return 3U;
    dev->state = PLAT_STATE_INITED;
    return 0U;
}

uint8_t plat_dev_start(plat_dev_t *dev)
{
    if ((dev == NULL) || (dev->ops == NULL)) return 1U;
    if (dev->state != PLAT_STATE_INITED) return 2U;
    if ((dev->ops->start != NULL) && (dev->ops->start() != 0U)) return 3U;
    dev->state = PLAT_STATE_STARTED;
    return 0U;
}

uint8_t plat_dev_process(plat_dev_t *dev)
{
    if ((dev == NULL) || (dev->ops == NULL)) return 1U;
    if (dev->state != PLAT_STATE_STARTED) return 2U;
    if (dev->ops->process != NULL) return dev->ops->process();
    return 0U;
}

uint8_t plat_dev_stop(plat_dev_t *dev)
{
    if ((dev == NULL) || (dev->ops == NULL)) return 1U;
    if (dev->state != PLAT_STATE_STARTED) return 2U;
    if ((dev->ops->stop != NULL) && (dev->ops->stop() != 0U)) return 3U;
    dev->state = PLAT_STATE_STOPPED;
    return 0U;
}

uint8_t plat_dev_deinit(plat_dev_t *dev)
{
    if ((dev == NULL) || (dev->ops == NULL)) return 1U;
    if ((dev->state != PLAT_STATE_INITED) &&
        (dev->state != PLAT_STATE_STARTED) &&
        (dev->state != PLAT_STATE_STOPPED)) {
        return 2U;
    }
    if ((dev->ops->deinit != NULL) && (dev->ops->deinit() != 0U)) return 3U;
    dev->state = PLAT_STATE_DEINITED;
    return 0U;
}

/* ========== 批量驱动（返回失败个数） ========== */

uint8_t plat_devmgr_init_all(void)
{
    uint8_t fail = 0U;
    plat_dev_t *p = s_dev_head;
    while (p != NULL) {
        if (plat_dev_init(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

uint8_t plat_devmgr_start_all(void)
{
    uint8_t fail = 0U;
    plat_dev_t *p = s_dev_head;
    while (p != NULL) {
        if (plat_dev_start(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

uint8_t plat_devmgr_process_all(void)
{
    uint8_t fail = 0U;
    plat_dev_t *p = s_dev_head;
    while (p != NULL) {
        if (plat_dev_process(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

uint8_t plat_devmgr_stop_all(void)
{
    uint8_t fail = 0U;
    plat_dev_t *p = s_dev_head;
    while (p != NULL) {
        if (plat_dev_stop(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

uint8_t plat_devmgr_deinit_all(void)
{
    uint8_t fail = 0U;
    plat_dev_t *p = s_dev_head;
    while (p != NULL) {
        if (plat_dev_deinit(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

#endif /* PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE */
