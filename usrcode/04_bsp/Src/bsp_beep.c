/**
 ******************************************************************************
 * @file    bsp_beep.c
 * @brief   有源蜂鸣器驱动实现 — NPN三极管驱动，高电平响(SET=响)
 *
 *  当前硬件：
 *    BEEP = PA5
 *    NPN三极管Q1驱动，基极经R12接PA5，R11下拉
 *    蜂鸣器正极接VCC5V，负极接Q1集电极，Q1发射极接地
 *
 *  电平逻辑：
 *    PA5 = HIGH(SET)   -> Q1基极正偏 -> 集电极-发射极导通 -> 蜂鸣器得电发声
 *    PA5 = LOW(RESET)  -> Q1基极零偏 -> 集电极-发射极截止 -> 蜂鸣器失电静音
 *
 *  GPIO配置由CubeMX MX_GPIO_Init()完成：
 *    PA5 = 推挽输出，默认低电平(RESET)，低速，无上拉下拉
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_BEEP_ENABLE

#include "bsp_beep.h"
#include "main.h"

/* ==========================================================================
 *  内部辅助：引脚映射（与CubeMX main.h中的BEEP_Pin/BEEP_GPIO_Port解耦）
 * ========================================================================== */

static GPIO_TypeDef *BSP_BEEP_GetPort(void)
{
    return BEEP_GPIO_Port;
}

static uint16_t BSP_BEEP_GetPin(void)
{
    return BEEP_Pin;
}

/* ==========================================================================
 *  公共API
 * ========================================================================== */

void BSP_BEEP_On(void)
{
    GPIO_TypeDef *port = BSP_BEEP_GetPort();
    uint16_t      pin  = BSP_BEEP_GetPin();
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);  /* 低电平 -> 响（低电平触发） */
}

void BSP_BEEP_Off(void)
{
    GPIO_TypeDef *port = BSP_BEEP_GetPort();
    uint16_t      pin  = BSP_BEEP_GetPin();
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);   /* 高电平 -> 不响（低电平触发） */
}

void BSP_BEEP_Toggle(void)
{
    GPIO_TypeDef *port = BSP_BEEP_GetPort();
    uint16_t      pin  = BSP_BEEP_GetPin();
    HAL_GPIO_TogglePin(port, pin);
}

uint8_t BSP_BEEP_GetState(void)
{
    GPIO_TypeDef *port = BSP_BEEP_GetPort();
    uint16_t      pin  = BSP_BEEP_GetPin();
    GPIO_PinState state;

    state = HAL_GPIO_ReadPin(port, pin);
    /* 低电平响: RESET=响(返回1), SET=不响(返回0) */
    return (state == GPIO_PIN_RESET) ? 1U : 0U;
}

void BSP_BEEP_Beep(uint32_t ms)
{
    BSP_BEEP_On();
    HAL_Delay(ms);
    BSP_BEEP_Off();
}

#endif /* BSP_BEEP_ENABLE */
