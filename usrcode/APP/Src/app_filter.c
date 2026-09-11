/**
 ******************************************************************************
 * @file    app_filter.c
 * @brief   滑动平均滤波器实现
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_FILTER_ENABLE

#include "app_filter.h"
#include <stddef.h>

void APP_FILTER_Init(MovingAvgFilter_t *f)
{
    uint8_t i;

    if (f == NULL) {
        return;
    }
    for (i = 0U; i < APP_FILTER_WINDOW_SIZE; i++) {
        f->buffer[i] = 0.0f;
    }
    f->index = 0U;
    f->count = 0U;
    f->sum   = 0.0f;
}

float APP_FILTER_Update(MovingAvgFilter_t *f, float new_value)
{
    if (f == NULL) {
        return 0.0f;
    }

    /* 窗口已满：减去最旧值 */
    if (f->count >= APP_FILTER_WINDOW_SIZE) {
        f->sum -= f->buffer[f->index];
    }

    /* 写入新值 */
    f->buffer[f->index] = new_value;
    f->sum += new_value;
    f->index = (f->index + 1U) % APP_FILTER_WINDOW_SIZE;
    if (f->count < APP_FILTER_WINDOW_SIZE) {
        f->count++;
    }

    return f->sum / (float)f->count;
}

float APP_FILTER_Get(const MovingAvgFilter_t *f)
{
    if ((f == NULL) || (f->count == 0U)) {
        return 0.0f;
    }
    return f->sum / (float)f->count;
}

#endif /* APP_FILTER_ENABLE */
