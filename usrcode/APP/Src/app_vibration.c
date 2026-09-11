/**
 ******************************************************************************
 * @file    app_vibration.c
 * @brief   振动特征提取实现 — 时域统计特征
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_VIBRATION_ENABLE

#include "app_vibration.h"
#include <string.h>
#include <math.h>

void APP_VIB_FeatureInit(VibrationWindow_t *vw)
{
    if (vw == NULL) {
        return;
    }
    memset(vw->buffer, 0, sizeof(vw->buffer));
    vw->index = 0U;
    vw->count = 0U;
    vw->ready = 0U;
}

void APP_VIB_FeaturePush(VibrationWindow_t *vw, float value)
{
    if (vw == NULL) {
        return;
    }
    vw->buffer[vw->index] = value;
    vw->index = (uint16_t)((vw->index + 1U) % APP_VIB_WINDOW_SIZE);
    if (vw->count < APP_VIB_WINDOW_SIZE) {
        vw->count++;
        if (vw->count >= APP_VIB_WINDOW_SIZE) {
            vw->ready = 1U;
        }
    }
}

uint8_t APP_VIB_FeatureCalculate(const VibrationWindow_t *vw, VibrationFeatures_t *feat)
{
    uint16_t i;
    uint16_t n;
    float sum = 0.0f, sum_sq = 0.0f, sum_abs = 0.0f, sum_4th = 0.0f;
    float peak = 0.0f;
    float mean, rms, mean_abs, variance, std_dev, kurtosis;

    if ((vw == NULL) || (feat == NULL)) {
        return 0U;
    }
    if (vw->count < 10U) {
        return 0U;   /* 数据太少不计算 */
    }

    n = vw->count;

    /* 单遍统计：和/平方和/绝对值/四次方/峰值 */
    for (i = 0U; i < n; i++) {
        float v = vw->buffer[i];
        float abs_v = fabsf(v);
        sum    += v;
        sum_sq += v * v;
        sum_abs += abs_v;
        sum_4th += v * v * v * v;
        if (abs_v > peak) {
            peak = abs_v;
        }
    }

    mean     = sum / (float)n;
    rms      = sqrtf(sum_sq / (float)n);
    mean_abs = sum_abs / (float)n;

    variance = sum_sq / (float)n - mean * mean;
    if (variance < 0.0f) {
        variance = 0.0f;
    }
    std_dev = sqrtf(variance);

    /* 峭度 = E[(x-μ)^4] / σ^4，正常振动≈3（高斯分布）；std_dev过小置3 */
    if (std_dev > 0.001f) {
        float sum_4th_centered = 0.0f;
        for (i = 0U; i < n; i++) {
            float d = vw->buffer[i] - mean;
            sum_4th_centered += d * d * d * d;
        }
        kurtosis = (sum_4th_centered / (float)n) /
                   (std_dev * std_dev * std_dev * std_dev);
    } else {
        kurtosis = 3.0f;
    }

    feat->rms             = rms;
    feat->peak            = peak;
    feat->kurtosis        = kurtosis;
    feat->mean_abs        = mean_abs;
    feat->crest_factor    = (rms > 0.001f) ? (peak / rms) : 0.0f;
    feat->waveform_factor = (mean_abs > 0.001f) ? (rms / mean_abs) : 0.0f;

    return 1U;
}

uint8_t APP_VIB_FeatureIsReady(const VibrationWindow_t *vw)
{
    if (vw == NULL) {
        return 0U;
    }
    return vw->ready;
}

/* ==========================================================================
 *  全局振动窗口
 * ========================================================================== */
static VibrationWindow_t s_global_win;

void APP_VIB_GlobalPush(float value)
{
    /* 首次调用自动初始化（静态变量初始为零，等价于清空窗口） */
    APP_VIB_FeaturePush(&s_global_win, value);
}

uint8_t APP_VIB_GlobalCompute(VibrationFeatures_t *feat)
{
    return APP_VIB_FeatureCalculate(&s_global_win, feat);
}

uint8_t APP_VIB_GlobalIsReady(void)
{
    return APP_VIB_FeatureIsReady(&s_global_win);
}

#endif /* APP_VIBRATION_ENABLE */
