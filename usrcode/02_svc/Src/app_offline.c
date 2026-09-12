/**
 ******************************************************************************
 * @file    app_offline.c
 * @brief   断网补传缓冲实现 — RAM环形缓冲
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_OFFLINE_ENABLE

#include "app_offline.h"
#include <string.h>

static OfflineBuffer_t s_offline_buf;

void APP_OFFLINE_Init(void)
{
    memset(&s_offline_buf, 0, sizeof(s_offline_buf));
}

uint8_t APP_OFFLINE_Push(const APP_Diag_Snapshot_t *snap)
{
    if (snap == NULL) {
        return 0U;
    }

    if (s_offline_buf.count >= APP_OFFLINE_BUF_SIZE) {
        /* 缓冲区满：覆盖最旧数据 */
        s_offline_buf.tail = (uint16_t)((s_offline_buf.tail + 1U) % APP_OFFLINE_BUF_SIZE);
        s_offline_buf.count--;
    }

    s_offline_buf.buffer[s_offline_buf.head] = *snap;
    s_offline_buf.head = (uint16_t)((s_offline_buf.head + 1U) % APP_OFFLINE_BUF_SIZE);
    s_offline_buf.count++;

    return 1U;
}

uint8_t APP_OFFLINE_Pop(APP_Diag_Snapshot_t *snap)
{
    if ((snap == NULL) || (s_offline_buf.count == 0U)) {
        return 0U;
    }

    *snap = s_offline_buf.buffer[s_offline_buf.tail];
    s_offline_buf.tail = (uint16_t)((s_offline_buf.tail + 1U) % APP_OFFLINE_BUF_SIZE);
    s_offline_buf.count--;

    return 1U;
}

uint16_t APP_OFFLINE_Count(void)
{
    return s_offline_buf.count;
}

#endif /* APP_OFFLINE_ENABLE */
