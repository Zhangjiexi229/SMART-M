/**
 ******************************************************************************
 * @file    app_alarm.c
 * @brief   告警管理器实现 + 告警任务（联动+故障诊断）
 *
 *  继电器联动语义（与工程 app_relay 一致）：
 *    吸合(ON)  = 电机电源回路导通（COM/NO串接）
 *    断开(OFF) = 电机断电保护
 *    告警触发 -> 断开继电器（断电保护）；恢复 -> 自动重新合闸
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_ALARM_ENABLE && APP_TASKS_ENABLE

#include "app_alarm.h"
#include "app_diag.h"
#include "app_config.h"
#if APP_FAULT_ENABLE
#include "app_fault.h"
#endif
#if APP_VIBRATION_ENABLE
#include "app_vibration.h"
#endif
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
#include "app_relay.h"
#endif
#if BSP_BEEP_ENABLE
#include "bsp_beep.h"
#endif
#include "bsp_uart.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

/* 迟滞比例：恢复时数值需低于阈值的 (1-HYSTERESIS) 倍 */
#define APP_ALARM_HYSTERESIS  0.10f

/* 告警状态机 */
static AlarmStatus_t s_alarm_status;

/* 任务周期 */
#define APP_ALARM_PERIOD_MS     100U
#define APP_ALARM_DIAG_DIV      10U    /* 每10拍=1秒做一次故障诊断 */

void APP_ALARM_Init(void)
{
    s_alarm_status.active      = 0U;
    s_alarm_status.level       = ALARM_NONE;
    s_alarm_status.source_mask = 0U;
    s_alarm_status.start_time  = 0U;
    s_alarm_status.duration    = 0U;
    g_alarm_active             = 0U;
#if APP_FAULT_ENABLE
    APP_FAULT_Init();
#endif
}

void APP_ALARM_Update(const APP_Diag_Snapshot_t *data)
{
    uint8_t new_mask  = 0U;
    uint8_t new_level = ALARM_NONE;
    uint8_t can_recover;

    if (data == NULL) {
        return;
    }

    /* ---- 三路独立检测：超阈值=CRITICAL，超80%=WARNING ---- */

    /* 温度 */
    if (data->sht_valid && (data->temperature > g_threshold.temp_high)) {
        new_mask |= ALARM_SRC_TEMP;
        new_level = ALARM_CRITICAL;
    } else if (data->sht_valid && (data->temperature > g_threshold.temp_high * 0.8f)) {
        new_mask |= ALARM_SRC_TEMP;
        if (new_level < ALARM_WARNING) new_level = ALARM_WARNING;
    }

    /* 振动 */
    if (data->imu_valid && (data->vibration > g_threshold.vib_high)) {
        new_mask |= ALARM_SRC_VIB;
        new_level = ALARM_CRITICAL;
    } else if (data->imu_valid && (data->vibration > g_threshold.vib_high * 0.8f)) {
        new_mask |= ALARM_SRC_VIB;
        if (new_level < ALARM_WARNING) new_level = ALARM_WARNING;
    }

    /* 电流 */
    if (data->ina_valid && (data->current > g_threshold.curr_high)) {
        new_mask |= ALARM_SRC_CURR;
        new_level = ALARM_CRITICAL;
    } else if (data->ina_valid && (data->current > g_threshold.curr_high * 0.8f)) {
        new_mask |= ALARM_SRC_CURR;
        if (new_level < ALARM_WARNING) new_level = ALARM_WARNING;
    }

    /* ---- 状态机：带迟滞的触发/恢复 ---- */
    if ((s_alarm_status.active == 0U) && (new_level >= ALARM_CRITICAL)) {
        /* 触发告警 */
        s_alarm_status.active      = 1U;
        s_alarm_status.level       = new_level;
        s_alarm_status.source_mask = new_mask;
        s_alarm_status.start_time  = (uint32_t)xTaskGetTickCount();
        s_alarm_status.duration    = 0U;
        g_alarm_active             = 1U;
        BSP_UART1_Printf("[ALARM] TRIGGERED: mask=0x%02X level=%d\r\n",
                         (unsigned)new_mask, (unsigned)new_level);
    } else if (s_alarm_status.active != 0U) {
        /* 更新告警等级与源 */
        s_alarm_status.level       = new_level;
        s_alarm_status.source_mask = new_mask;
        s_alarm_status.duration    = (uint32_t)xTaskGetTickCount() - s_alarm_status.start_time;

        /* 恢复条件：所有指标都低于阈值的 (1-HYSTERESIS) 倍 */
        can_recover = 1U;
        if (data->sht_valid && (data->temperature > g_threshold.temp_high * (1.0f - APP_ALARM_HYSTERESIS))) {
            can_recover = 0U;
        }
        if (data->imu_valid && (data->vibration > g_threshold.vib_high * (1.0f - APP_ALARM_HYSTERESIS))) {
            can_recover = 0U;
        }
        if (data->ina_valid && (data->current > g_threshold.curr_high * (1.0f - APP_ALARM_HYSTERESIS))) {
            can_recover = 0U;
        }

        if (can_recover != 0U) {
            s_alarm_status.active      = 0U;
            s_alarm_status.level       = ALARM_NONE;
            s_alarm_status.source_mask = 0U;
            g_alarm_active             = 0U;
            BSP_UART1_Printf("[ALARM] RECOVERED, duration=%ums\r\n",
                             (unsigned)s_alarm_status.duration);
        }
    }
}

uint8_t APP_ALARM_IsActive(void)
{
    return s_alarm_status.active;
}

void APP_ALARM_GetStatus(AlarmStatus_t *status)
{
    if (status != NULL) {
        *status = s_alarm_status;
    }
}

/* ==========================================================================
 *  告警任务
 * ========================================================================== */
void APP_ALARM_Task(void *argument)
{
    APP_Diag_Snapshot_t data;
    uint8_t was_active = 0U;
    uint8_t tick_div = 0U;
#if APP_VIBRATION_ENABLE
    VibrationFeatures_t s_vib_features;
#endif

    (void)argument;
    BSP_UART1_Printf("[ALARM] Task started (period=%ums, hysteresis=%u%%)\r\n",
                     (unsigned)APP_ALARM_PERIOD_MS, (unsigned)(APP_ALARM_HYSTERESIS * 100.0f));

    for (;;) {
        /* 1. 读取最新诊断快照（振动窗口由 QMI8658 任务 10ms 喂样） */
        APP_DIAG_GetSnapshot(&data);

        /* 2. 更新告警状态机（记录触发/恢复瞬间） */
        was_active = s_alarm_status.active;
        APP_ALARM_Update(&data);

        /* 4. 触发瞬间：联动动作（断电保护 + 蜂鸣器 + 提示） */
        if ((s_alarm_status.active != 0U) && (was_active == 0U)) {
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
            APP_Relay_Control(0U);   /* 断开继电器 = 电机断电保护 */
#endif
#if BSP_BEEP_ENABLE
            BSP_BEEP_Beep(100U);    /* 蜂鸣器短鸣一下（不一直响） */
#endif
            BSP_UART1_Printf("[ALARM] ACTION: relay=OFF(motor power cut) buzzer=ON\r\n");
        }

        /* 5. 恢复瞬间：解除联动 */
        if ((s_alarm_status.active == 0U) && (was_active != 0U)) {
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
            APP_Relay_Control(1U);   /* 重新合闸 = 电机恢复供电 */
#endif
#if BSP_BEEP_ENABLE
            BSP_BEEP_Off();          /* 确保蜂鸣器关 */
#endif
            BSP_UART1_Printf("[ALARM] RECOVER: relay=ON buzzer=OFF\r\n");
        }

        /* 6. 每秒一次：振动特征计算 + 故障诊断 + 打印 */
        tick_div++;
        if (tick_div >= APP_ALARM_DIAG_DIV) {
            tick_div = 0U;
#if APP_VIBRATION_ENABLE && APP_FAULT_ENABLE
            if (APP_VIB_GlobalIsReady()) {
                if (APP_VIB_GlobalCompute(&s_vib_features)) {
                    FaultDiagnosis_t diag;
                    APP_FAULT_Update(&data, &s_vib_features);
                    APP_FAULT_GetResult(&diag);
                    if (diag.status != APP_FAULT_NORMAL) {
                        // BSP_UART1_Printf("[DIAG] %s (conf=%u%%) - %s\r\n",
                        //                  APP_FAULT_GetStatusString(diag.status),
                        //                  (unsigned)(diag.confidence * 100.0f),
                        //                  diag.description);
                    }
                }
            }
#endif
        }

        /* 7. 阻塞，释放CPU */
        osDelay(APP_ALARM_PERIOD_MS);
    }
}

#endif /* APP_ALARM_ENABLE && APP_TASKS_ENABLE */
