/**
 ******************************************************************************
 * @file    app_key_matrix.c
 * @brief   4x4矩阵键盘应用层实现 — 消抖 + 按键动作 + 阈值设置状态机
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_KEY_MATRIX_ENABLE && APP_TASKS_ENABLE

#include "app_key_matrix.h"
#include "bsp_key_matrix.h"
#include "bsp_uart.h"
#include "bsp_delay.h"
#include "main.h"
#if APP_CONFIG_ENABLE
#include "app_config.h"
#endif
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
#include "app_relay.h"
#endif
#if BSP_BEEP_ENABLE
#include "bsp_beep.h"
#endif
#if APP_OLED_ENABLE && BSP_OLED_ENABLE
#include "app_oled.h"
#endif
#include "cmsis_os.h"

/* 任务周期与消抖次数 */
#define KEYM_PERIOD_MS         50U
#define KEYM_DEBOUNCE_COUNT    3U

/* 按键动作函数（在 app_oled/app_config 条件编译保护下调用） */
static void KeyMatrix_Action(uint8_t key);

/**
 * @brief  按键动作分发（含阈值设置模式状态机）
 */
static void KeyMatrix_Action(uint8_t key)
{
    switch (g_set_mode) {
    case SET_MODE_EXIT:
        /* ---- 正常模式 ---- */
        switch (key) {
        case KEYM_KEY_A:      /* 切换OLED页面 */
#if APP_OLED_ENABLE && BSP_OLED_ENABLE
            APP_OLED_PageSwitch();
#endif
            break;
        case KEYM_KEY_B:      /* 手动控制继电器 */
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
            APP_Relay_Control((uint8_t)((APP_Relay_GetState() == 0U) ? 1U : 0U));
            BSP_UART1_Printf("[KEY] Relay %s\r\n",
                             (APP_Relay_GetState() != 0U) ? "ON" : "OFF");
#endif
            break;
        case KEYM_KEY_C:      /* 蜂鸣器测试 */
#if BSP_BEEP_ENABLE
            BSP_BEEP_Beep(100U);
#endif
            break;
        case KEYM_KEY_STAR:   /* 进入阈值设置 */
#if APP_CONFIG_ENABLE && APP_OLED_ENABLE && BSP_OLED_ENABLE
            g_set_mode = SET_MODE_SELECT;
            g_set_selected = 0U;
            APP_OLED_ShowPage(APP_OLED_PAGE_THRESHOLD);
            BSP_UART1_Printf("[KEY] Enter threshold settings\r\n");
#endif
            break;
        default:
            break;
        }
        break;

    case SET_MODE_SELECT:
        /* ---- 选择阈值项 ---- */
        switch (key) {
        case KEYM_KEY_2:      /* 上移 */
            g_set_selected = (uint8_t)((g_set_selected + 2U) % 3U);
            break;
        case KEYM_KEY_8:      /* 下移 */
            g_set_selected = (uint8_t)((g_set_selected + 1U) % 3U);
            break;
        case KEYM_KEY_A:      /* 确认进入调整 */
            g_set_mode = SET_MODE_ADJUST;
            break;
        case KEYM_KEY_STAR:   /* 退出设置 */
            g_set_mode = SET_MODE_EXIT;
#if APP_OLED_ENABLE && BSP_OLED_ENABLE
            APP_OLED_ShowPage(APP_OLED_PAGE_REALTIME);
#endif
            BSP_UART1_Printf("[KEY] Exit settings\r\n");
            break;
        default:
            break;
        }
        break;

    case SET_MODE_ADJUST:
        /* ---- 调整数值 ---- */
        switch (key) {
        case KEYM_KEY_2:      /* 加 */
        case KEYM_KEY_POUND:
            switch (g_set_selected) {
            case 0U: g_threshold.temp_high += 0.1f; break;   /* 温度：0.1℃ 步进 */
            case 1U: g_threshold.vib_high  += 0.1f; break;
            case 2U: g_threshold.curr_high += 0.1f; break;
            default: break;
            }
            break;
        case KEYM_KEY_8:      /* 减（下限保护） */
        case KEYM_KEY_STAR:
            switch (g_set_selected) {
            case 0U:
                if (g_threshold.temp_high > 10.0f) g_threshold.temp_high -= 0.1f;
                break;
            case 1U:
                if (g_threshold.vib_high > 0.2f) g_threshold.vib_high -= 0.1f;
                break;
            case 2U:
                if (g_threshold.curr_high > 0.2f) g_threshold.curr_high -= 0.1f;
                break;
            default: break;
            }
            break;
        case KEYM_KEY_A:      /* 确认保存到Flash */
#if APP_CONFIG_ENABLE
            (void)APP_CONFIG_Save();
#endif
            g_set_mode = SET_MODE_SELECT;
            BSP_UART1_Printf("[KEY] Threshold saved\r\n");
            break;
        case KEYM_KEY_B:      /* 取消返回 */
            g_set_mode = SET_MODE_SELECT;
            break;
        default:
            break;
        }
        break;

    default:
        g_set_mode = SET_MODE_EXIT;
        break;
    }
}

/**
 * @brief  矩阵键盘扫描任务：50ms周期，连续3次同键消抖
 */
void APP_KeyMatrix_Task(void *argument)
{
    uint8_t last_key = 0U;
    uint8_t stable_count = 0U;

    (void)argument;
    BSP_UART1_Printf("[KEYM] Matrix key task started (period=%ums, debounce=%u)\r\n",
                     (unsigned)KEYM_PERIOD_MS, (unsigned)KEYM_DEBOUNCE_COUNT);

    for (;;) {
        uint8_t key = BSP_KeyMatrix_Scan();

        if (key == 0U) {
            /* 无按键：清除消抖计数 */
            last_key = 0U;
            stable_count = 0U;
        } else if (key == last_key) {
            /* 连续相同：计数 */
            stable_count++;
            if (stable_count >= KEYM_DEBOUNCE_COUNT) {
                stable_count = 0U;   /* 触发一次动作 */
                KeyMatrix_Action(key);
            }
        } else {
            /* 新按键：重新计数 */
            last_key = key;
            stable_count = 1U;
            BSP_UART1_Printf("[KEYM] key=%c\r\n", key);
        }

        osDelay(KEYM_PERIOD_MS);
    }
}

#endif /* APP_KEY_MATRIX_ENABLE && APP_TASKS_ENABLE */
