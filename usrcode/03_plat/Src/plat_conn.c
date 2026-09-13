/**
 ******************************************************************************
 * @file    plat_conn.c
 * @brief   平台抽象层（PLAT）— 连接器抽象实现（注册表 + 统一状态机）
 ******************************************************************************
 */
#include "module_cfg.h"
#if PLAT_CONN_ENABLE

#include "plat_conn.h"

#include <string.h>

#define PLAT_CONN_MAX_SLOTS   4U   /* 最大连接器数量 */

static plat_conn_t *s_conns[PLAT_CONN_MAX_SLOTS];

uint8_t plat_conn_register(plat_conn_t *c)
{
    uint16_t i;

    if (c == NULL) return 1U;

    /* 重名拒绝 */
    if (plat_conn_find(c->name) != NULL) return 2U;

    for (i = 0U; i < PLAT_CONN_MAX_SLOTS; i++) {
        if (s_conns[i] == NULL) {
            s_conns[i] = c;
            c->state  = PLAT_CONN_STATE_OFFLINE;
            return 0U;
        }
    }
    return 3U;   /* 表满 */
}

uint8_t plat_conn_unregister(plat_conn_t *c)
{
    uint16_t i;

    if (c == NULL) return 1U;
    for (i = 0U; i < PLAT_CONN_MAX_SLOTS; i++) {
        if (s_conns[i] == c) {
            s_conns[i] = NULL;
            return 0U;
        }
    }
    return 2U;
}

plat_conn_t *plat_conn_find(const char *name)
{
    uint16_t i;

    if (name == NULL) return NULL;
    for (i = 0U; i < PLAT_CONN_MAX_SLOTS; i++) {
        if ((s_conns[i] != NULL) && (s_conns[i]->name != NULL) &&
            (strcmp(s_conns[i]->name, name) == 0)) {
            return s_conns[i];
        }
    }
    return NULL;
}

uint16_t plat_conn_count(void)
{
    uint16_t i, n = 0U;
    for (i = 0U; i < PLAT_CONN_MAX_SLOTS; i++) {
        if (s_conns[i] != NULL) n++;
    }
    return n;
}

uint8_t plat_conn_status(plat_conn_t *c)
{
    if ((c == NULL) || (c->ops == NULL) || (c->ops->status == NULL)) return 0U;
    return c->ops->status();
}

plat_conn_state_t plat_conn_refresh_state(plat_conn_t *c)
{
    uint8_t bits;

    if (c == NULL) return PLAT_CONN_STATE_OFFLINE;

    bits = plat_conn_status(c);
    if (bits & 0x04U) {
        c->state = PLAT_CONN_STATE_ONLINE;      /* 应用层就绪 */
    } else if (bits & (0x01U | 0x02U)) {
        c->state = PLAT_CONN_STATE_LINKING;     /* 建链中 */
    } else {
        c->state = PLAT_CONN_STATE_OFFLINE;     /* 完全离线 */
    }
    return c->state;
}

#endif /* PLAT_CONN_ENABLE */
