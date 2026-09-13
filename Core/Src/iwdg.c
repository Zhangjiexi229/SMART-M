/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg.c
  * @brief   IWDG 独立看门狗配置 — 寄存器直操作实现（不依赖 HAL IWDG 驱动）
  *
  *  参数（与 pro_F407VE_v2.2a 一致）：
  *    Prescaler = /64（PR=4）
  *    Reload    = 1500
  *    超时      = (64 * 1500) / 32000 ≈ 3.0 秒（LSI≈32kHz）
  *
  *  说明：
  *    - IWDG 一旦启动不可停止，必须在超时前周期性喂狗（MX_IWDG_Refresh）
  *    - 喂狗由 app_watchdog 多任务心跳任务统一执行：
  *        所有关键任务心跳正常 → BSP_IWDG_Refresh() → MX_IWDG_Refresh()
  *    - bsp_esp8266.c 长阻塞（JoinAP/TCPConnect）期间经 BSP_ESP8266_WaitHook 喂狗
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "iwdg.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* IWDG init function */
void MX_IWDG_Init(void)
{
  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */

  /* 1. 解除写保护：向关键字寄存器写入 0x5555 */
  IWDG->KR = 0x5555U;

  /* 2. 配置预分频：/64（PR=4，LSI 32kHz → 512Hz 计数时钟） */
  IWDG->PR = 0x04U;

  /* 3. 配置重载值：1500 → 超时 = 1500/512 ≈ 2.93s（约 3 秒） */
  IWDG->RLR = 1500U;

  /* 4. 启动看门狗：写入 0xCCCC */
  IWDG->KR = 0xCCCCU;

  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */
}

/* USER CODE BEGIN 1 */

/**
  * @brief  Refresh the IWDG counter (feed the watchdog)
  * @note   Call periodically before the IWDG timeout (3s) expires.
  *         喂狗命令：向关键字寄存器写入 0xAAAA
  */
void MX_IWDG_Refresh(void)
{
  IWDG->KR = 0xAAAAU;
}

/* USER CODE END 1 */
