/**
 ******************************************************************************
 * @file    app_watchdog.c
 * @brief   多任务心跳检测 + IWDG 喂狗（移植自 pro_F407VE_v2.2a，任务ID按SMART-M定制）
 *
 *  机制：
 *    1. 每个关键任务在主循环中调用 Watchdog_Kick(id) 更新自己的心跳计数
 *    2. 看门狗任务每秒检查所有任务的心跳是否在递增
 *    3. 全部正常才调用 BSP_IWDG_Refresh() 喂狗
 *    4. 任一任务停止心跳 → 不喂狗 → IWDG 超时（3秒）复位系统
 *
 *  IWDG 超时设为 3 秒（Core/Src/iwdg.c），看门狗任务周期 1 秒，
 *  连续 5 秒无心跳才判死（容忍任务长 osDelay）。
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_WATCHDOG_ENABLE

#include "app_watchdog.h"
#include "bsp_iwdg.h"
#include "cmsis_os.h"
#if BSP_UART1_ENABLE
#include "bsp_uart.h"

/* 模块打印：受 APP_WATCHDOG_UART1_PRINTF_ENABLE 开关控制，关闭后编译为空 */
#if APP_WATCHDOG_UART1_PRINTF_ENABLE && BSP_UART1_ENABLE
#define WDT_Printf(fmt, ...)  BSP_UART1_Printf(fmt, ##__VA_ARGS__)
#else
#define WDT_Printf(fmt, ...)  ((void)0)
#endif
#endif

/* 各任务的心跳计数器（任务调用 Kick 时递增） */
static volatile uint32_t s_heartbeat[WDT_TASK_COUNT];

/* 上次检查时的心跳值（用于比较是否递增） */
static uint32_t s_prev_heartbeat[WDT_TASK_COUNT];

/* 连续无心跳计数器：连续 WDT_STALL_THRESHOLD 秒无心跳才判死（避免长osDelay误判） */
static uint8_t s_stall_count[WDT_TASK_COUNT];
#define WDT_STALL_THRESHOLD 5U

/* 任务活跃标记：任务创建成功后Register置1，watchdog只检查活跃任务 */
static uint8_t s_task_active[WDT_TASK_COUNT];
static uint8_t s_active_count = 0U;

void Watchdog_Kick(WDT_TaskID_t id)
{
    if ((uint32_t)id < WDT_TASK_COUNT) {
        s_heartbeat[id]++;
    }
}

void Watchdog_Register(WDT_TaskID_t id)
{
    if ((uint32_t)id < WDT_TASK_COUNT) {
        if (s_task_active[id] == 0U) {
            s_task_active[id] = 1U;
            s_active_count++;
        }
    }
}

void Watchdog_DelayWithKick(WDT_TaskID_t id, uint32_t ms)
{
    uint32_t remaining = ms;
    while (remaining > 0U) {
        uint32_t slice = (remaining > 500U) ? 500U : remaining;
        osDelay(slice);
        Watchdog_Kick(id);
        remaining -= slice;
    }
}

void APP_Watchdog_Task(void *argument)
{
    (void)argument;
    uint32_t i;
    uint8_t all_alive;
    uint32_t grace_cycles = 5U;  /* 启动宽限：前5秒直接喂狗，等其他任务初始化 */

    /* IWDG 已由 main.c 中的 MX_IWDG_Init() 启动（超时 3 秒），
     * 本任务只需定期喂狗。启动后先喂一次，确保初始化期间不复位。 */
    BSP_IWDG_Refresh();

    /* 初始化上次心跳值 */
    for (i = 0U; i < WDT_TASK_COUNT; i++) {
        s_prev_heartbeat[i] = s_heartbeat[i];
    }

#if BSP_UART1_ENABLE
    WDT_Printf("[WDT] Watchdog started, timeout=3s, monitoring %u/%u tasks\r\n",
                     (unsigned)s_active_count, (unsigned)WDT_TASK_COUNT);
#endif

    for (;;) {
        osDelay(1000U);   /* 每秒检查一次 */

        /* 启动宽限期：其他任务可能还在初始化，直接喂狗不检查心跳 */
        if (grace_cycles > 0U) {
            grace_cycles--;
            BSP_IWDG_Refresh();
            continue;
        }

        all_alive = 1U;
        for (i = 0U; i < WDT_TASK_COUNT; i++) {
            if (s_task_active[i] == 0U) {
                continue;   /* 任务未创建/未注册，跳过 */
            }
            if (s_heartbeat[i] == s_prev_heartbeat[i]) {
                /* 心跳未递增：累计 stall 计数，连续超过阈值才判死 */
                s_stall_count[i]++;
                if (s_stall_count[i] >= WDT_STALL_THRESHOLD) {
                    all_alive = 0U;
#if BSP_UART1_ENABLE
                    WDT_Printf("[WDT] Task %u stalled %us, skip feed\r\n",
                                     (unsigned)i, (unsigned)s_stall_count[i]);
#endif
                    break;
                }
            } else {
                s_stall_count[i] = 0U;  /* 心跳恢复，清零 */
            }
            s_prev_heartbeat[i] = s_heartbeat[i];
        }

        if (all_alive != 0U) {
            BSP_IWDG_Refresh();   /* 全部正常才喂狗 */
        }
        /* 有任务卡死则不喂狗，IWDG 超时后自动复位 */
    }
}

#endif /* APP_WATCHDOG_ENABLE */
