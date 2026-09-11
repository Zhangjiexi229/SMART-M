/**
 ******************************************************************************
 * @file    app_tasks.h
 * @brief   应用任务管理头文件 — 所有FreeRTOS任务的创建入口
 *
  集成点（CubeMX USER CODE区域）：
 *    1. APP_Init()              -> main.c USER CODE 2（BSP硬件初始化）
 *    2. APP_TASKS_CreateObjects() -> freertos.c RTOS_QUEUES（创建流缓冲等对象）
 *    3. APP_TASKS_CreateTasks()   -> freertos.c RTOS_THREADS（创建任务）
 *
 *  设计原则：任务与队列不在CubeMX中配置，统一在本模块创建，保持MX隔离。

 ******************************************************************************
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

/**
 * @brief  应用层初始化 — BSP硬件初始化，在main()中osKernelStart之前调用
 */
void APP_Init(void);

/**
 * @brief  创建所有RTOS对象（流缓冲、队列、信号量），在MX_FREERTOS_Init中调用
 */
void APP_TASKS_CreateObjects(void);

/**
 * @brief  创建所有应用任务，在MX_FREERTOS_Init中调用
 */
void APP_TASKS_CreateTasks(void);

#endif /* APP_TASKS_H */
