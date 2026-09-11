/**
 ******************************************************************************
 * @file    app_vibration.h
 * @brief   振动特征提取头文件 — 时域统计特征（RMS/峰值/峭度/峰值因子）
 *
 *  对应工作任务 Day6「振动特征提取」：
 *    QMI8658 输出瞬时加速度，需在一段时间窗口内计算统计特征用于故障判断。
 *
 *  特征物理意义（答辩可讲）：
 *    - RMS          均方根，反映振动总能量（轴承磨损/不平衡→升高）
 *    - 峰值         最大偏离
 *    - 峭度         反映冲击成分，正常电机≈3，点蚀/剥落时飙升到5~10
 *    - 峰值因子     峰值/RMS，对早期故障敏感（RMS未变时CF已升高）
 *    - 波形因子     RMS/平均绝对值
 *
 *  窗口：100 个采样点；10ms 采集周期 = 1 秒窗口，每秒更新一次特征
 ******************************************************************************
 */
#ifndef APP_VIBRATION_H
#define APP_VIBRATION_H

#include <stdint.h>

#define APP_VIB_WINDOW_SIZE   100U   /* 100个采样点，10ms周期=1秒窗口 */

/* 计算得到的特征集合 */
typedef struct {
    float rms;             /*!< 均方根（g） */
    float peak;            /*!< 峰值（g） */
    float kurtosis;        /*!< 峭度（正常≈3，故障>3） */
    float crest_factor;    /*!< 峰值因子 = 峰值/RMS */
    float waveform_factor; /*!< 波形因子 = RMS/平均绝对值 */
    float mean_abs;        /*!< 平均绝对值（g） */
} VibrationFeatures_t;

/* 采样窗口（环形缓冲） */
typedef struct {
    float    buffer[APP_VIB_WINDOW_SIZE];
    uint16_t index;
    uint16_t count;
    uint8_t  ready;        /* 缓冲区填满后置1 */
} VibrationWindow_t;

/**
 * @brief  窗口初始化（清零）
 * @param  vw  窗口指针
 */
void APP_VIB_FeatureInit(VibrationWindow_t *vw);

/**
 * @brief  推入一个振动采样值（由 QMI8658 任务每次采集后调用）
 * @param  vw     窗口指针
 * @param  value  振动模值（g）
 */
void APP_VIB_FeaturePush(VibrationWindow_t *vw, float value);

/**
 * @brief  基于当前窗口计算特征
 * @param  vw    窗口指针
 * @param  feat  输出特征结构
 * @retval 1=计算成功，0=数据太少（<10点）
 * @note   数据包含静态重力 1g；峭度计算带 std_dev 除零保护
 */
uint8_t APP_VIB_FeatureCalculate(const VibrationWindow_t *vw, VibrationFeatures_t *feat);

/**
 * @brief  窗口是否已填满（可用于"每秒计算一次"）
 * @param  vw  窗口指针
 * @retval 1=已满，0=未满
 */
uint8_t APP_VIB_FeatureIsReady(const VibrationWindow_t *vw);

/* ==========================================================================
 *  全局振动窗口（供 QMI8658 任务 10ms 喂样、告警任务每秒计算）
 *  10ms x 100点 = 1秒窗口，与工作任务 Day6 的窗口口径一致
 * ========================================================================== */

/**
 * @brief  推入一个振动采样值（QMI8658 任务每次采集后调用）
 * @param  value  振动模值（g）
 */
void APP_VIB_GlobalPush(float value);

/**
 * @brief  基于全局窗口计算特征（窗口未满或数据不足返回0）
 * @param  feat  输出特征结构
 * @retval 1=计算成功，0=数据不足
 */
uint8_t APP_VIB_GlobalCompute(VibrationFeatures_t *feat);

/**
 * @brief  全局窗口是否已填满
 * @retval 1=已满，0=未满
 */
uint8_t APP_VIB_GlobalIsReady(void);

#endif /* APP_VIBRATION_H */
