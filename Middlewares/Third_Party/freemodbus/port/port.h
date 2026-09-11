/*
 * FreeModbus Port: STM32F407 + HAL + CMSIS-RTOS2 (FreeRTOS)
 * 从站(Slave):  UART4 (PC10=TX, PC11=RX) + TIM7 + DE=PB1
 * 主站(Master): UART2 (PA2=TX,  PA3=RX)  + TIM6 + DE=PB0
 */
#ifndef _PORT_H
#define _PORT_H

#include "stm32f4xx_hal.h"
#include "mbconfig.h"
#include "cmsis_os.h"
#include <stdint.h>

#define INLINE
#define PR_BEGIN_EXTERN_C           extern "C" {
#define PR_END_EXTERN_C             }

#define ENTER_CRITICAL_SECTION()    taskENTER_CRITICAL()
#define EXIT_CRITICAL_SECTION()     taskEXIT_CRITICAL()

typedef uint8_t  BOOL;
typedef unsigned char UCHAR;
typedef char     CHAR;
typedef uint16_t USHORT;
typedef int16_t  SHORT;
typedef uint32_t ULONG;
typedef int32_t  LONG;

#ifndef TRUE
#define TRUE            1
#endif
#ifndef FALSE
#define FALSE           0
#endif

/* ===== 从站(Slave)硬件 ===== */
extern UART_HandleTypeDef huart4;
extern TIM_HandleTypeDef  htim7;
#define MB_SLAVE_DE_PORT       GPIOB
#define MB_SLAVE_DE_PIN        GPIO_PIN_1
#define MB_SLAVE_TX_MODE()     HAL_GPIO_WritePin(MB_SLAVE_DE_PORT, MB_SLAVE_DE_PIN, GPIO_PIN_SET)
#define MB_SLAVE_RX_MODE()     HAL_GPIO_WritePin(MB_SLAVE_DE_PORT, MB_SLAVE_DE_PIN, GPIO_PIN_RESET)

/* ===== 主站(Master)硬件 ===== */
extern UART_HandleTypeDef huart2;
extern TIM_HandleTypeDef  htim12;
#define MB_MASTER_DE_PORT      GPIOB
#define MB_MASTER_DE_PIN       GPIO_PIN_0
#define MB_MASTER_TX_MODE()    HAL_GPIO_WritePin(MB_MASTER_DE_PORT, MB_MASTER_DE_PIN, GPIO_PIN_SET)
#define MB_MASTER_RX_MODE()    HAL_GPIO_WritePin(MB_MASTER_DE_PORT, MB_MASTER_DE_PIN, GPIO_PIN_RESET)

/* ===== OS 事件队列（在 app_modbus.c 中创建） ===== */
extern osMessageQueueId_t mbSlaveEventQueueHandle;
extern osMessageQueueId_t mbMasterEventQueueHandle;
extern osSemaphoreId_t    mbMasterRunSemHandle;

#endif /* _PORT_H */
