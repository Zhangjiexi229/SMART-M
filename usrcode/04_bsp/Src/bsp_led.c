/**
 ******************************************************************************
 * @file    bsp_led.c
 * @brief   LED驱动实现 — 低电平点亮（RESET=亮，SET=灭）
 *
 *  当前硬件：
 *    LED1 = PG14
 *    LED2 = PG13
 *    LED3 = PG6
 *    LED4 = PG11
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_LED_ENABLE

#include "bsp_led.h"
#include "main.h"

static GPIO_TypeDef *BSP_LED_GetPort(BSP_LED_t led)
{
    switch (led) {
        case BSP_LED1:
            return LED1_GPIO_Port;
        case BSP_LED2:
            return LED2_GPIO_Port;
        case BSP_LED3:
            return LED3_GPIO_Port;
        case BSP_LED4:
            return LED4_GPIO_Port;
        default:
            return NULL;
    }
}

static uint16_t BSP_LED_GetPin(BSP_LED_t led)
{
    switch (led) {
        case BSP_LED1:
            return LED1_Pin;
        case BSP_LED2:
            return LED2_Pin;
        case BSP_LED3:
            return LED3_Pin;
        case BSP_LED4:
            return LED4_Pin;
        default:
            return 0U;
    }
}

void BSP_LED_On(BSP_LED_t led)
{
    GPIO_TypeDef *port = BSP_LED_GetPort(led);
    uint16_t pin = BSP_LED_GetPin(led);
    if (port != NULL) {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);   /* 低电平点亮 */
    }
}

void BSP_LED_Off(BSP_LED_t led)
{
    GPIO_TypeDef *port = BSP_LED_GetPort(led);
    uint16_t pin = BSP_LED_GetPin(led);
    if (port != NULL) {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);     /* 高电平熄灭 */
    }
}

void BSP_LED_Toggle(BSP_LED_t led)
{
    GPIO_TypeDef *port = BSP_LED_GetPort(led);
    uint16_t pin = BSP_LED_GetPin(led);
    if (port != NULL) {
        HAL_GPIO_TogglePin(port, pin);
    }
}

uint8_t BSP_LED_GetState(BSP_LED_t led)
{
    GPIO_TypeDef *port = BSP_LED_GetPort(led);
    uint16_t pin = BSP_LED_GetPin(led);
    GPIO_PinState state;

    if (port == NULL) {
        return 0U;
    }
    state = HAL_GPIO_ReadPin(port, pin);
    /* 低电平点亮: RESET=亮(返回1), SET=灭(返回0) */
    return (state == GPIO_PIN_RESET) ? 1U : 0U;
}

#endif /* BSP_LED_ENABLE */
