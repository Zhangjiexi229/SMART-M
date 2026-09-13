/**
 ******************************************************************************
 * @file    plat_svcmgr.c
 * @brief   平台抽象层（PLAT）— 服务管理器实现
 *
 *  功能：
 *    - 服务注册/注销/按名查找（单向链表）
 *    - 服务生命周期：init / start / process / stop / deinit
 *    - 依赖注入：服务启动前校验依赖设备已注册且已就绪
 ******************************************************************************
 */
#include "module_cfg.h"
#if PLAT_SVCMGR_ENABLE && PLAT_OBJ_ENABLE

#include "plat_mgr.h"
#include <string.h>

static plat_svc_t *s_svc_head = NULL;
static uint16_t s_svc_count = 0U;

/* ========== 注册 / 注销 / 查找 ========== */

uint8_t plat_svcmgr_register(plat_svc_t *svc)
{
    if ((svc == NULL) || (svc->name == NULL)) {
        return 1U;
    }
    if (plat_svcmgr_find(svc->name) != NULL) {
        return 2U;   /* 重名 */
    }
    svc->next  = s_svc_head;
    svc->state = PLAT_STATE_REGISTERED;
    s_svc_head = svc;
    s_svc_count++;
    return 0U;
}

uint8_t plat_svcmgr_unregister(plat_svc_t *svc)
{
    plat_svc_t **pp = &s_svc_head;

    if (svc == NULL) return 1U;
    while (*pp != NULL) {
        if (*pp == svc) {
            *pp = svc->next;
            svc->next  = NULL;
            svc->state = PLAT_STATE_NONE;
            if (s_svc_count > 0U) s_svc_count--;
            return 0U;
        }
        pp = &(*pp)->next;
    }
    return 1U;
}

plat_svc_t *plat_svcmgr_find(const char *name)
{
    plat_svc_t *p = s_svc_head;

    if (name == NULL) return NULL;
    while (p != NULL) {
        if ((p->name != NULL) && (strcmp(p->name, name) == 0)) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

uint16_t plat_svcmgr_count(void)
{
    return s_svc_count;
}

/* ========== 服务生命周期 ========== */

uint8_t plat_svc_register(plat_svc_t *svc)
{
    return plat_svcmgr_register(svc);
}

uint8_t plat_svc_init(plat_svc_t *svc)
{
    if ((svc == NULL) || (svc->ops == NULL)) return 1U;
    if (svc->state != PLAT_STATE_REGISTERED) return 2U;
    if ((svc->ops->init != NULL) && (svc->ops->init() != 0U)) return 3U;
    svc->state = PLAT_STATE_INITED;
    return 0U;
}

uint8_t plat_svc_start(plat_svc_t *svc)
{
    uint8_t i;

    if ((svc == NULL) || (svc->ops == NULL)) return 1U;
    if ((svc->state != PLAT_STATE_INITED) && (svc->state != PLAT_STATE_STOPPED)) {
        return 2U;
    }
    /* 依赖注入校验：依赖设备必须已注册且已就绪 */
    for (i = 0U; i < svc->dep_count; i++) {
        if (!plat_dev_is_ready(svc->deps[i])) return 3U;
    }
    if ((svc->ops->start != NULL) && (svc->ops->start() != 0U)) return 4U;
    svc->state = PLAT_STATE_STARTED;
    return 0U;
}

uint8_t plat_svc_process(plat_svc_t *svc)
{
    if ((svc == NULL) || (svc->ops == NULL)) return 1U;
    if (svc->state != PLAT_STATE_STARTED) return 2U;
    if (svc->ops->process != NULL) return svc->ops->process();
    return 0U;
}

uint8_t plat_svc_stop(plat_svc_t *svc)
{
    if ((svc == NULL) || (svc->ops == NULL)) return 1U;
    if (svc->state != PLAT_STATE_STARTED) return 2U;
    if ((svc->ops->stop != NULL) && (svc->ops->stop() != 0U)) return 3U;
    svc->state = PLAT_STATE_STOPPED;
    return 0U;
}

uint8_t plat_svc_deinit(plat_svc_t *svc)
{
    if ((svc == NULL) || (svc->ops == NULL)) return 1U;
    if ((svc->state != PLAT_STATE_INITED) &&
        (svc->state != PLAT_STATE_STARTED) &&
        (svc->state != PLAT_STATE_STOPPED)) {
        return 2U;
    }
    if ((svc->ops->deinit != NULL) && (svc->ops->deinit() != 0U)) return 3U;
    svc->state = PLAT_STATE_DEINITED;
    return 0U;
}

/* ========== 批量驱动（返回失败个数） ========== */

uint8_t plat_svcmgr_init_all(void)
{
    uint8_t fail = 0U;
    plat_svc_t *p = s_svc_head;
    while (p != NULL) {
        if (plat_svc_init(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

uint8_t plat_svcmgr_start_all(void)
{
    uint8_t fail = 0U;
    plat_svc_t *p = s_svc_head;
    while (p != NULL) {
        if (plat_svc_start(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

uint8_t plat_svcmgr_process_all(void)
{
    uint8_t fail = 0U;
    plat_svc_t *p = s_svc_head;
    while (p != NULL) {
        if (plat_svc_process(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

uint8_t plat_svcmgr_stop_all(void)
{
    uint8_t fail = 0U;
    plat_svc_t *p = s_svc_head;
    while (p != NULL) {
        if (plat_svc_stop(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

uint8_t plat_svcmgr_deinit_all(void)
{
    uint8_t fail = 0U;
    plat_svc_t *p = s_svc_head;
    while (p != NULL) {
        if (plat_svc_deinit(p) != 0U) fail++;
        p = p->next;
    }
    return fail;
}

#endif /* PLAT_SVCMGR_ENABLE && PLAT_OBJ_ENABLE */
