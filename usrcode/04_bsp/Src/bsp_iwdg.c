/**
 ******************************************************************************
 * @file    bsp_iwdg.c
 * @brief   独立看门狗 IWDG 驱动（寄存器直操作，见 Core/Src/iwdg.c）
 *
 *  配置参数（与 Core/Src/iwdg.c 一致）：
 *    Prescaler = /64
 *    Reload    = 1500
 *    超时      = (64 * 1500) / 32000 ≈ 3.0 秒
 *
 *  IWDG 在 main.c 的 MX_IWDG_Init() 中启动（调度器启动前），
 *  本文件仅提供喂狗接口，供 app_watchdog 多任务心跳监控调用。
 ******************************************************************************
 */
#include "module_cfg.h"

#if BSP_IWDG_ENABLE

#include "bsp_iwdg.h"
#include "iwdg.h"

/**
 * @brief  IWDG 初始化（已由 MX_IWDG_Init 在 main.c 中完成）
 * @note   保留此函数以兼容旧调用；实际初始化在 MX_IWDG_Init() 中。
 *         IWDG 一旦启动不可停止，超时固定为 3 秒（MX 配置）。
 * @param  timeout_ms  超时时间（忽略，以 MX 配置为准）
 */
void BSP_IWDG_Init(uint32_t timeout_ms)
{
    (void)timeout_ms;
    /* IWDG 已在 main.c 的 MX_IWDG_Init() 中配置并启动，
     * 此处无需重复操作。如需修改超时，请调整 Core/Src/iwdg.c 中
     * IWDG->PR / IWDG->RLR 参数。 */
}

/**
 * @brief  喂狗（重载 IWDG 计数器）
 * @note   必须在超时（3秒）内定期调用，否则系统复位。
 *         由 app_watchdog 任务在确认所有任务心跳正常后调用。
 */
void BSP_IWDG_Refresh(void)
{
    MX_IWDG_Refresh();
}

#endif /* BSP_IWDG_ENABLE */
