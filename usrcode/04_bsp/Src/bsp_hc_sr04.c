/**
 ******************************************************************************
 * @file    bsp_hc_sr04.c
 * @brief   HC-SR04 超声波测距模块驱动实现 — 轮询式测量
 *
 *  ▍测量流程：
 *    1. Trig 拉低 → 拉高12us → 拉低（触发）
 *    2. 等待 Echo 变高（超时30ms）
 *    3. 记录起始时间戳
 *    4. 等待 Echo 变低（超时30ms）
 *    5. 计算脉宽 → 距离 = 脉宽(us) × 17 ÷ 100 (mm)
 *    6. 限幅 20~4000mm
 *
 *  ▍距离计算公式推导：
 *    声速 = 340 m/s = 0.34 mm/us
 *    往返距离 = 0.34 × 脉宽(us) mm
 *    单程距离 = 0.34 × 脉宽 ÷ 2 = 0.17 × 脉宽 mm
 *    整数运算：脉宽 × 17 ÷ 100
 *
 *  ▍引脚（CubeMX已在main.h定义）：
 *    trig_Pin = GPIO_PIN_11, trig_GPIO_Port = GPIOC
 *    echo_Pin = GPIO_PIN_5,  echo_GPIO_Port = GPIOE
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_HC_SR04_ENABLE

#include "bsp_hc_sr04.h"
#include "bsp_delay.h"
#include "main.h"

/* ========== 引脚别名（使用CubeMX生成的宏） ========== */
#define TRIG_PIN        trig_Pin
#define TRIG_PORT       trig_GPIO_Port
#define ECHO_PIN        echo_Pin
#define ECHO_PORT       echo_GPIO_Port

/* ========== 引脚操作内联 ========== */
static inline void Trig_High(void) { HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_SET); }
static inline void Trig_Low(void)  { HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET); }
static inline uint8_t Echo_Read(void) { return (HAL_GPIO_ReadPin(ECHO_PORT, ECHO_PIN) == GPIO_PIN_SET) ? 1U : 0U; }

/* ========== GPIO 初始化 ========== */
void BSP_HC_SR04_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIOC 和 GPIOE 时钟 */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* Trig (PC11): 推挽输出 */
    GPIO_InitStruct.Pin   = TRIG_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(TRIG_PORT, &GPIO_InitStruct);
    Trig_Low();   /* 空闲状态：Trig低电平 */

    /* Echo (PE5): 浮空输入（模块内部已有上拉，外部也可加上拉） */
    GPIO_InitStruct.Pin   = ECHO_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(ECHO_PORT, &GPIO_InitStruct);
}

/* ========== 测距核心 ========== */
uint8_t BSP_HC_SR04_MeasureMm(uint16_t *distance_mm)
{
    uint32_t start_us;
    uint32_t pulse_us;
    uint32_t timeout;

    if (distance_mm == NULL) {
        return 2U;   /* 参数错误 */
    }

    /* ---- 第一步：发送触发脉冲 ---- */
    Trig_Low();
    BSP_DelayUs(2U);
    Trig_High();
    BSP_DelayUs(HC_SR04_TRIG_US);   /* 保持12us高电平（≥10us要求） */
    Trig_Low();

    /* ---- 第二步：等待 Echo 变高 ---- */
    timeout = HC_SR04_TIMEOUT_US;
    while (Echo_Read() == 0U) {
        if (timeout == 0U) {
            return 1U;   /* 超时：Echo一直为低，模块无响应 */
        }
        BSP_DelayUs(1U);
        timeout--;
    }

    /* ---- 第三步：记录起始时间，等待 Echo 变低 ---- */
    start_us = BSP_Delay_GetTickUs();
    timeout = HC_SR04_TIMEOUT_US;
    while (Echo_Read() == 1U) {
        if (timeout == 0U) {
            return 1U;   /* 超时：Echo一直为高，超出测量范围 */
        }
        BSP_DelayUs(1U);
        timeout--;
    }

    /* ---- 第四步：计算脉宽和距离 ---- */
    pulse_us = BSP_Delay_GetTickUs() - start_us;   /* 32位减法自动处理回绕 */

    /* 距离(mm) = 脉宽(us) × 0.17 = 脉宽 × 17 ÷ 100 */
    {
        uint32_t dist_mm = (pulse_us * 17U) / 100U;

        /* 限幅：有效范围 20~4000mm */
        if (dist_mm < HC_SR04_MIN_DIST_MM) {
            dist_mm = HC_SR04_MIN_DIST_MM;
        }
        if (dist_mm > HC_SR04_MAX_DIST_MM) {
            dist_mm = HC_SR04_MAX_DIST_MM;
        }
        *distance_mm = (uint16_t)dist_mm;
    }

    return 0U;   /* 成功 */
}

#endif /* BSP_HC_SR04_ENABLE */
