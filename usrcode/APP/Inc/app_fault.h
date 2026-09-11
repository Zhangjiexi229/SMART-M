/**
 ******************************************************************************
 * @file    app_fault.h
 * @brief   简易故障诊断规则引擎头文件 — 三态（正常/注意/异常）
 *
 *  对应工作任务 Day6「简易故障诊断规则」：
 *    基于振动特征 + 温度 + 电流，用规则引擎判断电机状态。
 *
 *  规则（旋转机械故障诊断经典指标）：
 *    - 过热：  温度 > 65℃ 异常 / > 50℃ 注意
 *    - 过载：  电流 > 5.0A 异常 / > 3.0A 注意
 *    - 轴承：  峭度>6 或 峰值因子>6 异常；>4 注意（冲击成分）
 *    - 不平衡：RMS>2.5g 且峭度正常(<4) 异常；>1.5g 注意
 *    - 多故障叠加：>=2 个注意级故障升级为异常
 *
 *  输出：状态(正常/注意/异常) + 故障类型位图 + 文字描述 + 置信度
 ******************************************************************************
 */
#ifndef APP_FAULT_H
#define APP_FAULT_H

#include <stdint.h>
#include "app_diag.h"
#include "app_vibration.h"

/* ========== 故障状态 ========== */
#define APP_FAULT_NORMAL   0   /* 正常 */
#define APP_FAULT_WARNING  1   /* 注意 */
#define APP_FAULT_ABNORMAL 2   /* 异常 */

/* ========== 故障类型位图 ========== */
#define APP_FAULT_TYPE_NONE       0U
#define APP_FAULT_TYPE_OVERHEAT   (1U << 0)   /* 过热 */
#define APP_FAULT_TYPE_OVERLOAD   (1U << 1)   /* 过载 */
#define APP_FAULT_TYPE_VIBRATION  (1U << 2)   /* 振动异常 */
#define APP_FAULT_TYPE_IMBALANCE  (1U << 3)   /* 不平衡（RMS高+峭度正常） */
#define APP_FAULT_TYPE_BEARING    (1U << 4)   /* 轴承故障（峭度高） */

/* 诊断结果 */
typedef struct {
    uint8_t  status;        /*!< 整体状态：APP_FAULT_NORMAL/WARNING/ABNORMAL */
    uint8_t  fault_mask;    /*!< 故障类型位图 */
    char     description[64]; /*!< 文字描述 */
    float    confidence;    /*!< 置信度 0~1 */
} FaultDiagnosis_t;

/**
 * @brief  故障诊断初始化
 */
void APP_FAULT_Init(void);

/**
 * @brief  更新诊断结果（由告警任务每秒调用一次）
 * @param  sensor  诊断快照（温度/电流）
 * @param  vib     振动特征（RMS/峭度/峰值因子）
 * @note   vib 为 NULL 时跳过振动诊断（特征窗口未满）
 */
void APP_FAULT_Update(const APP_Diag_Snapshot_t *sensor, const VibrationFeatures_t *vib);

/**
 * @brief  获取当前诊断结果
 * @param  result  输出诊断结构
 */
void APP_FAULT_GetResult(FaultDiagnosis_t *result);

/**
 * @brief  状态转字符串
 * @param  status  APP_FAULT_* 状态
 * @retval "Normal"/"Warning"/"Abnormal"/"Unknown"
 */
const char *APP_FAULT_GetStatusString(uint8_t status);

#endif /* APP_FAULT_H */
