/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg.h
  * @brief   This file contains all the function prototypes for
  *          the iwdg.c file
  * @note    移植自 pro_F407VE_v2.2a，采用寄存器直操作实现（不依赖 HAL IWDG 驱动）。
  *          IWDG 使用 LSI(~32kHz) 时钟，超时 3 秒。
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __IWDG_H__
#define __IWDG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Prototypes */
void MX_IWDG_Init(void);
void MX_IWDG_Refresh(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __IWDG_H__ */
