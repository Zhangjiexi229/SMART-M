/*
 * FreeModbus Port: 从站定时器 (TIM7, T3.5 帧间隔超时)
 * 注意: HAL_TIM_PeriodElapsedCallback 在 main.c 中统一定义，
 *       该文件仅提供从站定时器操作，由 main.c 回调调用 MB_Slave_TimerExpiredHandler()
 */
#include "port.h"
#include "mb.h"
#include "mbport.h"

#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0
extern void MB_Master_TimerExpiredHandler(void);
#endif

BOOL xMBPortTimersInit(USHORT usTimoutOutMicroseconds)
{
    /* TIM7 配置为 0.1ms/tick (PSC=8399) */
    uint32_t arr = usTimoutOutMicroseconds / 100;
    if (arr < 1) arr = 1;
    __HAL_TIM_SET_AUTORELOAD(&htim7, arr - 1);
    __HAL_TIM_SET_COUNTER(&htim7, 0);
    return TRUE;
}

void vMBPortTimersEnable(void)
{
    __HAL_TIM_SET_COUNTER(&htim7, 0);
    __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&htim7);
}

void vMBPortTimersDisable(void)
{
    HAL_TIM_Base_Stop_IT(&htim7);
}

/* 从站定时器超时处理 — 在 main.c 的 HAL_TIM_PeriodElapsedCallback 中调用 */
void MB_Slave_TimerExpiredHandler(void)
{
    pxMBPortCBTimerExpired();
}
