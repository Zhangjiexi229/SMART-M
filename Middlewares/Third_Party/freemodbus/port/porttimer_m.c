/*
 * FreeModbus Port: 主站定时器 (TIM6)
 * 支持三种定时模式: T35帧间隔 / 响应超时 / 广播转换延时
 * TIM6 配置: 0.1ms/tick (PSC=8399, 84MHz/8400=10kHz)
 */
#include "port.h"
#include "mb.h"
#include "mb_m.h"
#include "mbport.h"

#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0

static USHORT usT35TimeOut50us;  /* T3.5 时间，单位 50us */

BOOL xMBMasterPortTimersInit(USHORT usTimeOut50us)
{
    usT35TimeOut50us = usTimeOut50us;
    return TRUE;
}

void vMBMasterPortTimersT35Enable(void)
{
    /* T3.5: usTimeOut50us * 50us → 转换为 0.1ms tick */
    uint32_t arr = (usT35TimeOut50us * 50) / 100;
    if (arr < 1) arr = 1;
    vMBMasterSetCurTimerMode(MB_TMODE_T35);
    __HAL_TIM_SET_AUTORELOAD(&htim12, arr - 1);
    __HAL_TIM_SET_COUNTER(&htim12, 0);
    __HAL_TIM_CLEAR_FLAG(&htim12, TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&htim12);
}

void vMBMasterPortTimersConvertDelayEnable(void)
{
    /* 广播帧转换延时: MB_MASTER_DELAY_MS_CONVERT ms */
    uint32_t arr = MB_MASTER_DELAY_MS_CONVERT * 10;  /* ms → 0.1ms */
    vMBMasterSetCurTimerMode(MB_TMODE_CONVERT_DELAY);
    __HAL_TIM_SET_AUTORELOAD(&htim12, arr - 1);
    __HAL_TIM_SET_COUNTER(&htim12, 0);
    __HAL_TIM_CLEAR_FLAG(&htim12, TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&htim12);
}

void vMBMasterPortTimersRespondTimeoutEnable(void)
{
    /* 响应超时: MB_MASTER_TIMEOUT_MS_RESPOND ms */
    uint32_t arr = MB_MASTER_TIMEOUT_MS_RESPOND * 10;
    vMBMasterSetCurTimerMode(MB_TMODE_RESPOND_TIMEOUT);
    __HAL_TIM_SET_AUTORELOAD(&htim12, arr - 1);
    __HAL_TIM_SET_COUNTER(&htim12, 0);
    __HAL_TIM_CLEAR_FLAG(&htim12, TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&htim12);
}

void vMBMasterPortTimersDisable(void)
{
    HAL_TIM_Base_Stop_IT(&htim12);
}

/* 主站定时器超时处理 — 在 main.c 的 HAL_TIM_PeriodElapsedCallback 中调用 */
void MB_Master_TimerExpiredHandler(void)
{
    (void)pxMBMasterPortCBTimerExpired();
}

#endif /* MB_MASTER_RTU_ENABLED || MB_MASTER_ASCII_ENABLED */
