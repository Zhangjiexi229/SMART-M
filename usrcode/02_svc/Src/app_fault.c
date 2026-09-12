/**
 ******************************************************************************
 * @file    app_fault.c
 * @brief   简易故障诊断规则引擎实现
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_FAULT_ENABLE

#include "app_fault.h"
#include <stdio.h>
#include <string.h>

/* 诊断阈值（可根据实际电机调整） */
#define APP_FAULT_TEMP_WARNING    50.0f
#define APP_FAULT_TEMP_ABNORMAL   65.0f
#define APP_FAULT_CURR_WARNING     3.0f
#define APP_FAULT_CURR_ABNORMAL    5.0f
#define APP_FAULT_VIB_RMS_WARNING  1.5f
#define APP_FAULT_VIB_RMS_ABNORMAL 2.5f
#define APP_FAULT_KURTOSIS_WARNING 4.0f
#define APP_FAULT_KURTOSIS_ABNORMAL 6.0f
#define APP_FAULT_CREST_WARNING    4.0f
#define APP_FAULT_CREST_ABNORMAL   6.0f

static FaultDiagnosis_t s_diagnosis;

void APP_FAULT_Init(void)
{
    s_diagnosis.status     = APP_FAULT_NORMAL;
    s_diagnosis.fault_mask = APP_FAULT_TYPE_NONE;
    strcpy(s_diagnosis.description, "System Normal");
    s_diagnosis.confidence = 1.0f;
}

void APP_FAULT_Update(const APP_Diag_Snapshot_t *sensor, const VibrationFeatures_t *vib)
{
    uint8_t new_status = APP_FAULT_NORMAL;
    uint8_t new_mask   = APP_FAULT_TYPE_NONE;
    char desc[64]      = "System Normal";
    uint8_t warning_count = 0U;
    float confidence = 0.5f;

    if (sensor == NULL) {
        return;
    }

    /* 1. 温度诊断 */
    if (sensor->temperature > APP_FAULT_TEMP_ABNORMAL) {
        new_status = APP_FAULT_ABNORMAL;
        new_mask |= APP_FAULT_TYPE_OVERHEAT;
        strcpy(desc, "Overheat! Check cooling");
    } else if (sensor->temperature > APP_FAULT_TEMP_WARNING) {
        if (new_status < APP_FAULT_WARNING) new_status = APP_FAULT_WARNING;
        new_mask |= APP_FAULT_TYPE_OVERHEAT;
        strcpy(desc, "Temp rising, monitor");
    }

    /* 2. 电流诊断 */
    if (sensor->current > APP_FAULT_CURR_ABNORMAL) {
        new_status = APP_FAULT_ABNORMAL;
        new_mask |= APP_FAULT_TYPE_OVERLOAD;
        strcpy(desc, "Overload! Check load");
    } else if (sensor->current > APP_FAULT_CURR_WARNING) {
        if (new_status < APP_FAULT_WARNING) new_status = APP_FAULT_WARNING;
        new_mask |= APP_FAULT_TYPE_OVERLOAD;
        if (new_status == APP_FAULT_NORMAL) strcpy(desc, "Current high, check");
    }

    /* 3. 振动诊断（需要特征数据） */
    if ((vib != NULL) && (vib->rms > 0.0f)) {
        /* 轴承故障：峭度高 + 峰值因子高（冲击成分） */
        if ((vib->kurtosis > APP_FAULT_KURTOSIS_ABNORMAL) ||
            (vib->crest_factor > APP_FAULT_CREST_ABNORMAL)) {
            new_status = APP_FAULT_ABNORMAL;
            new_mask |= APP_FAULT_TYPE_BEARING | APP_FAULT_TYPE_VIBRATION;
            strcpy(desc, "Bearing fault! Kurtosis high");
        } else if ((vib->kurtosis > APP_FAULT_KURTOSIS_WARNING) ||
                   (vib->crest_factor > APP_FAULT_CREST_WARNING)) {
            if (new_status < APP_FAULT_WARNING) new_status = APP_FAULT_WARNING;
            new_mask |= APP_FAULT_TYPE_BEARING | APP_FAULT_TYPE_VIBRATION;
            if (new_status <= APP_FAULT_WARNING) strcpy(desc, "Bearing wear early sign");
        }

        /* 不平衡/不对中：RMS 高 + 峭度正常(<4) */
        if ((vib->rms > APP_FAULT_VIB_RMS_ABNORMAL) &&
            (vib->kurtosis < APP_FAULT_KURTOSIS_WARNING)) {
            new_status = APP_FAULT_ABNORMAL;
            new_mask |= APP_FAULT_TYPE_IMBALANCE | APP_FAULT_TYPE_VIBRATION;
            strcpy(desc, "Imbalance/misalignment!");
        } else if ((vib->rms > APP_FAULT_VIB_RMS_WARNING) &&
                   (vib->kurtosis < APP_FAULT_KURTOSIS_WARNING)) {
            if (new_status < APP_FAULT_WARNING) new_status = APP_FAULT_WARNING;
            new_mask |= APP_FAULT_TYPE_IMBALANCE | APP_FAULT_TYPE_VIBRATION;
            if (new_status == APP_FAULT_WARNING) strcpy(desc, "Vibration rising, check balance");
        }
    }

    /* 4. 综合判断：多个注意级故障叠加升级为异常 */
    if (new_mask & APP_FAULT_TYPE_OVERHEAT)  warning_count++;
    if (new_mask & APP_FAULT_TYPE_OVERLOAD)  warning_count++;
    if (new_mask & APP_FAULT_TYPE_VIBRATION) warning_count++;
    if ((warning_count >= 2U) && (new_status == APP_FAULT_WARNING)) {
        new_status = APP_FAULT_ABNORMAL;
        strcpy(desc, "Multiple faults! Immediate check");
    }

    /* 5. 置信度（简单规则：越异常置信度越高） */
    if (new_status == APP_FAULT_ABNORMAL) confidence = 0.9f;
    else if (new_status == APP_FAULT_WARNING) confidence = 0.7f;
    else confidence = 0.95f;

    s_diagnosis.status  = new_status;
    s_diagnosis.fault_mask = new_mask;
    strncpy(s_diagnosis.description, desc, sizeof(s_diagnosis.description) - 1U);
    s_diagnosis.description[sizeof(s_diagnosis.description) - 1U] = '\0';
    s_diagnosis.confidence = confidence;
}

void APP_FAULT_GetResult(FaultDiagnosis_t *result)
{
    if (result != NULL) {
        *result = s_diagnosis;
    }
}

const char *APP_FAULT_GetStatusString(uint8_t status)
{
    switch (status) {
    case APP_FAULT_NORMAL:   return "Normal";
    case APP_FAULT_WARNING:  return "Warning";
    case APP_FAULT_ABNORMAL: return "Abnormal";
    default:                 return "Unknown";
    }
}

#endif /* APP_FAULT_ENABLE */
