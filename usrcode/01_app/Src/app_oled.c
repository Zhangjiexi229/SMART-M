/**
 ******************************************************************************
 * @file    app_oled.c
 * @brief   OLED显示应用层实现 — 毕设三页面 + 告警反白闪烁
 *
 *  显示布局（6x8小字体，21字符/行，8行）：
 *    实时页: 页0 "Motor Monitor"    页2 "Temp: xx.x C"
 *            页4 "Vib : x.xx g"     页6 "Curr: x.xx A"
 *            页7 告警源/诊断状态（告警: T V I；正常: Normal 等）
 *    阈值页: 页0 "Threshold Set"    页2/4/6 三项（选中项前加 ">"）
 *            页7 操作提示
 *    状态页: 页0 "System Status"    页2 "WiFi: xxx"  页4 "Relay: xxx"
 *            页6 "Alarm: xxx"
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_OLED_ENABLE && APP_TASKS_ENABLE && BSP_OLED_ENABLE

#include "app_oled.h"
#include "bsp_oled.h"
#include "bsp_uart.h"
#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>
#if APP_DIAG_ENABLE
#include "app_diag.h"
#endif
#if APP_CONFIG_ENABLE
#include "app_config.h"
#endif
#if APP_FAULT_ENABLE
#include "app_fault.h"
#endif
#if APP_ALARM_ENABLE
#include "app_alarm.h"
#endif
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
#include "app_relay.h"
#endif
#if APP_MQTT_ENABLE
#include "app_mqtt.h"
#endif

/* ========== 互斥锁 ========== */
static osMutexId_t s_oled_mutex;

/* ========== 页面状态 ========== */
static volatile APP_OLED_Page_t s_page = APP_OLED_PAGE_REALTIME;
static volatile uint8_t s_blink_tick = 0U;      /* 告警闪烁节拍 */
static volatile uint8_t s_inverted = 0U;        /* 当前是否反色 */

/**
 * @brief  创建 OLED 写互斥锁
 */
void APP_OLED_CreateMutex(void)
{
    s_oled_mutex = osMutexNew(NULL);
    if (s_oled_mutex == NULL) {
        /* 锁创建失败：后续写操作不加锁，仍可工作但有并发风险 */
    }
}

/**
 * @brief  获取互斥锁（阻塞等待，最多100ms）
 * @return 1=获取成功，0=超时
 */
static uint8_t APP_OLED_Lock(void)
{
    if (s_oled_mutex == NULL) {
        return 1U;
    }
    return (osMutexAcquire(s_oled_mutex, 100U) == osOK) ? 1U : 0U;
}

/**
 * @brief  释放互斥锁
 */
static void APP_OLED_Unlock(void)
{
    if (s_oled_mutex != NULL) {
        (void)osMutexRelease(s_oled_mutex);
    }
}

/* ==========================================================================
 *  页面渲染（均在互斥锁保护下调用）
 * ========================================================================== */

/**
 * @brief  渲染实时数据页
 */
static void APP_OLED_RenderRealtime(void)
{
    char buf[24];
    APP_Diag_Snapshot_t snap;
#if APP_FAULT_ENABLE
    FaultDiagnosis_t diag;
#endif

    APP_DIAG_GetSnapshot(&snap);

    BSP_OLED_ShowString(0, 0, "Motor Monitor");

    /* 温度（SHT30） */
    if (snap.sht_valid) {
        snprintf(buf, sizeof(buf), "Temp: %5.1f C", (double)snap.temperature);
    } else {
        snprintf(buf, sizeof(buf), "Temp:   --.- C");
    }
    BSP_OLED_ShowString(2, 0, buf);

    /* 振动（QMI8658 模值g） */
    if (snap.imu_valid) {
        snprintf(buf, sizeof(buf), "Vib : %5.2f g", (double)snap.vibration);
    } else {
        snprintf(buf, sizeof(buf), "Vib :  --.-- g");
    }
    BSP_OLED_ShowString(4, 0, buf);

    /* 电流（INA226） */
    if (snap.ina_valid) {
        snprintf(buf, sizeof(buf), "Curr: %5.2f A", (double)snap.current);
    } else {
        snprintf(buf, sizeof(buf), "Curr:  --.-- A");
    }
    BSP_OLED_ShowString(6, 0, buf);

    /* 页7：告警源 或 诊断状态 */
#if APP_ALARM_ENABLE
    if (g_alarm_active) {
        AlarmStatus_t st;
        uint8_t col = 0U;
        APP_ALARM_GetStatus(&st);
        BSP_OLED_ShowString(7, 0, "ALARM!");
        if (st.source_mask & ALARM_SRC_TEMP) { BSP_OLED_ShowString(7, 48, "T"); }
        if (st.source_mask & ALARM_SRC_VIB)  { BSP_OLED_ShowString(7, 54, "V"); }
        if (st.source_mask & ALARM_SRC_CURR) { BSP_OLED_ShowString(7, 60, "I"); }
        (void)col;
    } else
#endif
#if APP_FAULT_ENABLE
    {
        APP_FAULT_GetResult(&diag);
        switch (diag.status) {
        case APP_FAULT_NORMAL:
            BSP_OLED_ShowString(7, 0, "System OK");
            break;
        case APP_FAULT_WARNING:
            BSP_OLED_ShowString(7, 0, "Status: Warn!");
            break;
        case APP_FAULT_ABNORMAL:
            BSP_OLED_ShowString(7, 0, "ABNORMAL!!");
            break;
        default:
            BSP_OLED_ShowString(7, 0, "System OK");
            break;
        }
        /* 故障类型缩写 H(过热) O(过载) V(振动) B(轴承) */
        if (diag.fault_mask & APP_FAULT_TYPE_OVERHEAT)  BSP_OLED_ShowString(7, 90, "H");
        if (diag.fault_mask & APP_FAULT_TYPE_OVERLOAD)  BSP_OLED_ShowString(7, 96, "O");
        if (diag.fault_mask & APP_FAULT_TYPE_VIBRATION) BSP_OLED_ShowString(7, 102, "V");
        if (diag.fault_mask & APP_FAULT_TYPE_BEARING)   BSP_OLED_ShowString(7, 108, "B");
    }
#else
    {
        BSP_OLED_ShowString(7, 0, "System OK");
    }
#endif
}

/**
 * @brief  渲染阈值设置页（选中项高亮）
 */
static void APP_OLED_RenderThreshold(void)
{
    const char *labels[3] = { "Temp:", "Vib :", "Curr:" };
    float values[3];
    const char *units[3] = { "C", "g", "A" };
    uint8_t y_pos[3] = { 2U, 4U, 6U };
    char buf[24];
    uint8_t i;

    values[0] = g_threshold.temp_high;
    values[1] = g_threshold.vib_high;
    values[2] = g_threshold.curr_high;

    BSP_OLED_ShowString(0, 0, "Threshold Set");

    for (i = 0U; i < 3U; i++) {
        if ((g_set_mode != SET_MODE_EXIT) && (g_set_selected == i)) {
            BSP_OLED_ShowString(y_pos[i], 0, ">");
        } else {
            BSP_OLED_ShowString(y_pos[i], 0, " ");
        }
        BSP_OLED_ShowString(y_pos[i], 6, labels[i]);
        snprintf(buf, sizeof(buf), "%4.1f %s", (double)values[i], units[i]);
        BSP_OLED_ShowString(y_pos[i], 42, buf);
    }

    /* 页7 操作提示 */
    if (g_set_mode == SET_MODE_EXIT) {
        BSP_OLED_ShowString(7, 0, "*: settings");
    } else if (g_set_mode == SET_MODE_SELECT) {
        BSP_OLED_ShowString(7, 0, "2/8:sel A:ok *:exit");
    } else {
        BSP_OLED_ShowString(7, 0, "2/8:+/- A:save B:back");
    }
}

/**
 * @brief  渲染系统状态页
 */
static void APP_OLED_RenderStatus(void)
{
    char buf[24];

    BSP_OLED_ShowString(0, 0, "System Status");

    /* WiFi 状态（MQTT任务状态：bit2=MQTT已连接） */
#if APP_MQTT_ENABLE
    snprintf(buf, sizeof(buf), "WiFi: %s",
             (APP_MQTT_GetStatus() & 0x04U) ? "online" : "offline");
#else
    snprintf(buf, sizeof(buf), "WiFi: --");
#endif
    BSP_OLED_ShowString(2, 0, buf);

    /* 继电器状态 */
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
    snprintf(buf, sizeof(buf), "Relay: %s",
             (APP_Relay_GetState() != 0U) ? "ON" : "OFF");
#else
    snprintf(buf, sizeof(buf), "Relay: --");
#endif
    BSP_OLED_ShowString(4, 0, buf);

    /* 告警状态 */
#if APP_ALARM_ENABLE
    snprintf(buf, sizeof(buf), "Alarm: %s",
             g_alarm_active ? "active" : "none");
#else
    snprintf(buf, sizeof(buf), "Alarm: --");
#endif
    BSP_OLED_ShowString(6, 0, buf);

    BSP_OLED_ShowString(7, 0, "A:page B:relay C:beep");
}

/* ==========================================================================
 *  公共 API
 * ========================================================================== */
void APP_OLED_Init(void)
{
    BSP_UART1_Printf("[OLED] init start\r\n");
    BSP_OLED_Init();   /* SSD1306初始化 + 清屏 */
    BSP_UART1_Printf("[OLED] bsp init ok\r\n");
    /* 不再做全屏点亮0xFF硬件测试：电荷泵+8192像素全亮是启动期最大电流事件，
     * 电池+面包板供电场景下会拉垮3.3V轨导致芯片锁死（现象：卡死+屏全黑） */
    BSP_OLED_Clear();
    BSP_UART1_Printf("[OLED] clear ok\r\n");

    s_page = APP_OLED_PAGE_REALTIME;
    s_blink_tick = 0U;
    s_inverted = 0U;
    BSP_UART1_Printf("[OLED] app init ok\r\n");
}

void APP_OLED_PageSwitch(void)
{
    APP_OLED_Page_t next = (APP_OLED_Page_t)((s_page + 1U) % APP_OLED_PAGE_MAX);
    APP_OLED_ShowPage(next);
}

void APP_OLED_ShowPage(APP_OLED_Page_t page)
{
    if (page >= APP_OLED_PAGE_MAX) {
        return;
    }
    /* 页面切换时复位反色，避免残留反色状态 */
    if (!APP_OLED_Lock()) { return; }
    if (s_inverted) {
        BSP_OLED_InvertDisplay(0U);
        s_inverted = 0U;
    }
    s_page = page;
    APP_OLED_Unlock();
}

/* ==========================================================================
 *  显示任务
 * ========================================================================== */
void APP_OLED_DisplayTask(void *argument)
{
    (void)argument;
    BSP_UART1_Printf("[OLED] Display task started (period=100ms)\r\n");

    for (;;) {
        if (!APP_OLED_Lock()) {
            osDelay(100U);
            continue;
        }

        /* 渲染当前页 */
        switch (s_page) {
        case APP_OLED_PAGE_REALTIME:
            APP_OLED_RenderRealtime();
            break;
        case APP_OLED_PAGE_THRESHOLD:
            APP_OLED_RenderThreshold();
            break;
        case APP_OLED_PAGE_STATUS:
            APP_OLED_RenderStatus();
            break;
        default:
            break;
        }

        /* 告警反白闪烁：每5拍(500ms)切换一次反色 */
#if APP_ALARM_ENABLE
        if (g_alarm_active) {
            s_blink_tick++;
            if ((s_blink_tick % 5U) == 0U) {
                s_inverted = (s_inverted == 0U) ? 1U : 0U;
                BSP_OLED_InvertDisplay(s_inverted);
            }
        } else if (s_inverted) {
            s_inverted = 0U;
            BSP_OLED_InvertDisplay(0U);
        }
#else
        (void)s_blink_tick;
        (void)s_inverted;
#endif

        APP_OLED_Unlock();
        osDelay(100U);
    }
}

/* ==========================================================================
 *  兼容旧调用：DisplayTask 接管屏幕后不再绘制（保留签名兼容DHT11/光敏任务）
 * ========================================================================== */
void APP_OLED_UpdateTempHum(uint8_t temp_int, uint8_t temp_dec,
                            uint8_t hum_int,  uint8_t hum_dec)
{
    (void)temp_int; (void)temp_dec; (void)hum_int; (void)hum_dec;
}

void APP_OLED_UpdateLight(uint16_t adc_value, const char *level_str)
{
    (void)adc_value; (void)level_str;
}

void APP_OLED_ShowError(void)
{
}

#endif /* APP_OLED_ENABLE && APP_TASKS_ENABLE && BSP_OLED_ENABLE */
