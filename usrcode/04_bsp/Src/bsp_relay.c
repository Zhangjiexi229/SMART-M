/**
 ******************************************************************************
 * @file    bsp_relay.c
 * @brief   继电器驱动实现 — PA4 GPIO控制
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_RELAY_ENABLE

#include "bsp_relay.h"
#include "main.h"

void BSP_RELAY_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin   = RELAY_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RELAY_GPIO_Port, &GPIO_InitStruct);

    /* 初始断开 */
    BSP_RELAY_Off();
}

void BSP_RELAY_On(void)
{
#if BSP_RELAY_ACTIVE_HIGH
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_SET);
#else
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET);
#endif
}

void BSP_RELAY_Off(void)
{
#if BSP_RELAY_ACTIVE_HIGH
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_SET);
#endif
}

void BSP_RELAY_Toggle(void)
{
    if (BSP_RELAY_GetState() != 0U) {
        BSP_RELAY_Off();
    } else {
        BSP_RELAY_On();
    }
}

uint8_t BSP_RELAY_GetState(void)
{
#if BSP_RELAY_ACTIVE_HIGH
    return (HAL_GPIO_ReadPin(RELAY_GPIO_Port, RELAY_Pin) == GPIO_PIN_SET) ? 1U : 0U;
#else
    return (HAL_GPIO_ReadPin(RELAY_GPIO_Port, RELAY_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
#endif
}

#endif /* BSP_RELAY_ENABLE */
