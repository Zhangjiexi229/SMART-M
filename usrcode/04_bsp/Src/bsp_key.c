/**
 ******************************************************************************
 * @file    bsp_key.c
 * @brief   按键驱动实现 — 外部上拉输入，按下为低电平（RESET=PRESSED）
 *
 *  消抖原理（计数器法）：
 *    每次 Scan 读取原始电平，若与稳定状态不同则计数器+1；
 *    连续 BSP_KEY_DEBOUNCE_CNT 次不一致则确认状态变更，
 *    若新状态为 PRESSED 则置位 press_evt 事件标志。
 *    采样与稳定状态一致时计数器清零。
 *
 *  当前硬件：
 *    KEY0 = PG2, KEY1 = PG3, KEY2 = PG4, KEY3 = PG5
 *    外部上拉电阻，按下接GND（低电平有效）
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_KEY_ENABLE

#include "bsp_key.h"
#include "main.h"

/**
 * @brief 消抖计数阈值：连续该次数采样一致才确认状态变更
 *        扫描周期10ms × 阈值2 = 20ms消抖时间
 */
#define BSP_KEY_DEBOUNCE_CNT   2U

/**
 * @brief 单键消抖上下文
 */
typedef struct {
    BSP_KEY_State_t stable;     /* 消抖后稳定状态 */
    uint8_t         cnt;        /* 状态变化计数器（0~BSP_KEY_DEBOUNCE_CNT） */
    uint8_t         press_evt;  /* 按下事件标志（释放->按下跃迁时置1，PopEvent清零） */
} key_ctx_t;

static key_ctx_t s_key_ctx[BSP_KEY_COUNT];

/* ==========================================================================
 *  内部辅助：引脚映射（与CubeMX main.h中的KEYx_Pin/KEYx_GPIO_Port解耦）
 * ========================================================================== */

static GPIO_TypeDef *BSP_KEY_GetPort(BSP_KEY_t key)
{
    switch (key) {
        case BSP_KEY0:
            return KEY0_GPIO_Port;
        case BSP_KEY1:
            return KEY1_GPIO_Port;
        case BSP_KEY2:
            return KEY2_GPIO_Port;
        case BSP_KEY3:
            return KEY3_GPIO_Port;
        default:
            return NULL;
    }
}

static uint16_t BSP_KEY_GetPin(BSP_KEY_t key)
{
    switch (key) {
        case BSP_KEY0:
            return KEY0_Pin;
        case BSP_KEY1:
            return KEY1_Pin;
        case BSP_KEY2:
            return KEY2_Pin;
        case BSP_KEY3:
            return KEY3_Pin;
        default:
            return 0U;
    }
}

/* ==========================================================================
 *  公共API
 * ========================================================================== */

void BSP_KEY_Init(void)
{
    uint8_t i;
    for (i = 0U; i < BSP_KEY_COUNT; i++) {
        s_key_ctx[i].stable    = BSP_KEY_RELEASED;
        s_key_ctx[i].cnt       = 0U;
        s_key_ctx[i].press_evt = 0U;
    }
}

BSP_KEY_State_t BSP_KEY_ReadRaw(BSP_KEY_t key)
{
    GPIO_TypeDef *port = BSP_KEY_GetPort(key);
    uint16_t      pin  = BSP_KEY_GetPin(key);
    GPIO_PinState raw;

    if (port == NULL) {
        return BSP_KEY_RELEASED;
    }
    raw = HAL_GPIO_ReadPin(port, pin);
    /* 外部上拉：按下=低电平(RESET)，释放=高电平(SET) */
    return (raw == GPIO_PIN_RESET) ? BSP_KEY_PRESSED : BSP_KEY_RELEASED;
}

void BSP_KEY_Scan(void)
{
    uint8_t i;
    for (i = 0U; i < BSP_KEY_COUNT; i++) {
        BSP_KEY_State_t raw = BSP_KEY_ReadRaw((BSP_KEY_t)i);
        key_ctx_t      *ctx = &s_key_ctx[i];

        if (raw != ctx->stable) {
            ctx->cnt++;
            if (ctx->cnt >= BSP_KEY_DEBOUNCE_CNT) {
                /* 确认状态变更 */
                if (raw == BSP_KEY_PRESSED) {
                    ctx->press_evt = 1U;   /* 释放->按下：置位事件 */
                }
                ctx->stable = raw;
                ctx->cnt    = 0U;
            }
        } else {
            ctx->cnt = 0U;   /* 采样一致，计数器清零 */
        }
    }
}

BSP_KEY_State_t BSP_KEY_GetState(BSP_KEY_t key)
{
    if ((uint8_t)key >= BSP_KEY_COUNT) {
        return BSP_KEY_RELEASED;
    }
    return s_key_ctx[(uint8_t)key].stable;
}

uint8_t BSP_KEY_PopEvent(BSP_KEY_t key)
{
    uint8_t evt;
    if ((uint8_t)key >= BSP_KEY_COUNT) {
        return 0U;
    }
    evt = s_key_ctx[(uint8_t)key].press_evt;
    s_key_ctx[(uint8_t)key].press_evt = 0U;   /* 读取即清除 */
    return evt;
}

#endif /* BSP_KEY_ENABLE */
