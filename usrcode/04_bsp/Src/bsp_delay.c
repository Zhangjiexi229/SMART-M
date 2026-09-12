/**
 ******************************************************************************
 * @file    bsp_delay.c
 * @brief   板级微秒延时驱动实现 — TIM2 32位自由计数器（1us/tick）
 *
 *  ▍为什么从 DWT 换成 TIM2：
 *    DWT->CYCCNT 属于内核调试/追踪块，调试器（JLINK/STLINK）下载后若以
 *    软件复位/向量捕获方式结束，DWT 可能未被干净重置而停止计数，导致
 *    BSP_DelayUs() 忙等死循环（烧录后 DHT11 不打印、按物理 RESET 才恢复）。
 *    TIM2 是普通外设定时器，由 RCC 定时器时钟（84MHz）驱动，与调试器状态
 *    完全无关，任何复位后都保证计数。
 *
 *  ▍TIM2 配置（F407 @168MHz，由 CubeMX 的 MX_TIM2_Init() 管理）：
 *    APB1 定时器时钟 = 84MHz，PSC=83 -> 1MHz（1 tick = 1us）
 *    ARR=0xFFFFFFFF，32 位自由计数器，约 71 分钟回绕一次，普通微秒延时无影响
 *    BSP_Delay_Init() 仅调用 HAL_TIM_Base_Start 启动计数，不再手动写寄存器
 *
 *  ▍本模块与其他延时资源的边界：
 *    - 不触碰 SysTick（FreeRTOS 独占）
 *    - 不触碰 TIM6（HAL 时基独占）
 *    - TIM2 做成"自由计数器"（只读 CNT），DHT11 / OLED 软件 I2C 可共享
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_DELAY_ENABLE

#include "bsp_delay.h"
#include "main.h"
#include "tim.h"    /* CubeMX 生成的 TIM2 句柄 htim2 */

#if BSP_DELAY_USE_TIM2

/**
 * @brief  启动 TIM2 自由计数器（1us/tick）
 * @note   TIM2 的时钟使能与参数配置由 CubeMX 的 MX_TIM2_Init() 完成：
 *           PSC=83（APB1 定时器时钟 84MHz -> 1MHz，1 tick = 1us）
 *           ARR=0xFFFFFFFF（32位自由计数，不产生更新事件）
 *           向上计数、内部时钟
 *         MX_TIM2_Init() 只配置不启动计数，本函数仅调用 HAL_TIM_Base_Start 使能。
 *         调用顺序：main.c 中 MX_TIM2_Init() -> APP_Init() -> BSP_Delay_Init()
 */
void BSP_Delay_Init(void)
{
    /* TIM2 已由 MX_TIM2_Init() 完成时钟使能与参数配置，此处仅启动计数 */
    HAL_TIM_Base_Start(&htim2);
}

/**
 * @brief  微秒级阻塞延时（基于 TIM2->CNT，32位无符号减法自动处理回绕）
 * @param  us  延时微秒数
 */
void BSP_DelayUs(uint32_t us)
{
    uint32_t start = TIM2->CNT;

    while ((TIM2->CNT - start) < us) {
        /* 空转等待 */
    }
}

/**
 * @brief  读取当前微秒计数值（1 tick = 1us）
 */
uint32_t BSP_Delay_GetTickUs(void)
{
    return TIM2->CNT;
}

/**
 * @brief  毫秒级阻塞延时（与 BSP_DelayUs 同一时间源）
 */
void BSP_DelayMs(uint32_t ms)
{
    BSP_DelayUs(ms * 1000U);   /* TIM2 为32位，1MHz下最大约4295s */
}

#else  /* BSP_DELAY_USE_TIM2 == 0：DWT 方案（默认不建议） */

/**
 * @brief  初始化 DWT 周期计数器
 */
void BSP_Delay_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
 * @brief  微秒级阻塞延时（基于 DWT->CYCCNT）
 * @param  us  延时微秒数
 * @note   注意：调试器下载后 DWT 可能不计数，本方案仅作备用
 */
void BSP_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < ticks) {
        /* 空转等待 */
    }
}

/**
 * @brief  读取当前微秒计数值（DWT 周期 -> us）
 */
uint32_t BSP_Delay_GetTickUs(void)
{
    return DWT->CYCCNT / (SystemCoreClock / 1000000U);
}

/**
 * @brief  毫秒级阻塞延时（与 BSP_DelayUs 同一时间源）
 */
void BSP_DelayMs(uint32_t ms)
{
    BSP_DelayUs(ms * 1000U);
}

#endif /* BSP_DELAY_USE_TIM2 */

#endif /* BSP_DELAY_ENABLE */
