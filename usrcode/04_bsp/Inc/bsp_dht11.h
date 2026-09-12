/**
 ******************************************************************************
 * @file    bsp_dht11.h
 * @brief   DHT11温湿度传感器驱动头文件 — PG9单总线协议 + 共用TIM2微秒计时
 *
 *  硬件连接：
 *    DHT11 DATA 引脚 -> PG9（单总线，需上拉电阻，模块通常已集成）
 *
 *  通信协议（单总线）：
 *    1. 主机起始信号：拉低 >=18ms，再拉高 20~40us
 *    2. DHT11响应：拉低 80us，再拉高 80us
 *    3. 数据传输：40位（5字节），每位以 50us 低电平开始，
 *       高电平 26~28us = 0，高电平 70us = 1
 *    4. 数据格式：湿度整数、湿度小数、温度整数、温度小数、校验和
 *    5. 校验和 = (湿度整+湿度小+温度整+温度小) & 0xFF
 *
 *  采样间隔：DHT11 最快 1Hz（1秒1次），建议 >=2秒
 *
 *  ▍延时设计（不冲突 / 不抢占 / 不相互影响）：
 *    - SysTick        -> FreeRTOS 独占，本驱动不碰
 *    - TIM6           -> HAL 时基（HAL_Delay），本驱动协议时序不使用
 *    - TIM2           -> 微秒/毫秒计时统一走 bsp_delay.h（BSP_DelayUs /
 *                        BSP_DelayMs / BSP_Delay_GetTickUs），
 *                        与 OLED 软件 I2C 共享一个自由计数器（只读 CNT）
 *    - 关中断临界区    -> 读位时序段短暂屏蔽中断，防任务抢占破坏位脉宽
 *
 *  设计原则：
 *    - BSP层不依赖RTOS（仅用 CMSIS __disable_irq/__enable_irq 做临界区）
 *    - 微秒延时依赖 BSP_Delay_Init()（bsp_delay.h），不直接改 SysTick
 ******************************************************************************
 */
#ifndef BSP_DHT11_H
#define BSP_DHT11_H

#include <stdint.h>

/**
 * @brief  DHT11 读取结果状态
 */
typedef enum {
    BSP_DHT11_OK           = 0,   /* 读取成功，校验通过 */
    BSP_DHT11_ERR_TIMEOUT  = 1,   /* 响应超时（未检测到DHT11） */
    BSP_DHT11_ERR_CHECKSUM = 2    /* 校验和错误 */
} BSP_DHT11_Status_t;

/**
 * @brief  DHT11 数据结构
 */
typedef struct {
    uint8_t  humidity_int;     /* 湿度整数部分 (%) */
    uint8_t  humidity_dec;     /* 湿度小数部分 (%) */
    uint8_t  temperature_int;  /* 温度整数部分 (℃) */
    uint8_t  temperature_dec;  /* 温度小数部分 (℃) */
} BSP_DHT11_Data_t;

/**
 * @brief  DHT11 读取失败阶段（用于定位"完全没回应"还是"数据读到但内容坏"）
 * @note   读取失败时串口打印的 stage 字段即此枚举值：
 *          1 -> DHT11 完全没回应（查硬件：供电/接线/上拉/模块）
 *          2/3 -> 响应只回了一半（查上拉电阻 / 接线接触）
 *          4/5 -> 有回应但数据段异常（查上拉 / 电源纹波 / 线长）
 *          6 -> 数据读到但校验和错误（同上，多为电气质量）
 */
typedef enum {
    BSP_DHT11_FAIL_NONE       = 0,   /* 读取成功，无失败 */
    BSP_DHT11_FAIL_RESP_LOW   = 1,   /* 响应起始低电平超时 */
    BSP_DHT11_FAIL_RESP_HIGH  = 2,   /* 响应高电平超时 */
    BSP_DHT11_FAIL_RESP_END   = 3,   /* 响应结束低电平超时 */
    BSP_DHT11_FAIL_BIT_LOW    = 4,   /* 数据位低电平超时 */
    BSP_DHT11_FAIL_BIT_HIGH   = 5,   /* 数据位高电平超时 */
    BSP_DHT11_FAIL_CHECKSUM   = 6    /* 校验和错误 */
} BSP_DHT11_FailStage_t;

/**
 * @brief  获取最近一次读取的失败阶段
 * @return 失败阶段枚举；读取成功时为 BSP_DHT11_FAIL_NONE
 */
BSP_DHT11_FailStage_t BSP_DHT11_GetLastFailStage(void);

/**
 * @brief  DHT11 初始化
 * @note   在任务调度器启动前调用（APP_Init），GPIO 由 MX_GPIO_Init 完成；
 *         TIM2 自由计数器由 BSP_Delay_Init() 统一初始化
 */
void BSP_DHT11_Init(void);

/**
 * @brief  读取一次DHT11温湿度数据（阻塞式，约 20ms+）
 * @param  data  输出参数，读取到的温湿度数据
 * @return BSP_DHT11_OK=成功，其他=失败
 * @note   调用间隔应 >=1秒，建议2秒；读取失败时data内容不变；
 *         内部在时序关键段会短暂关中断（约4~5ms），期间不调用RTOS API
 */
BSP_DHT11_Status_t BSP_DHT11_Read(BSP_DHT11_Data_t *data);

/**
 * @brief  DHT11 硬件自检（串口诊断用）
 * @note   启动时调用一次，打印：GPIO环回 / 空闲总线电平 / 应答窗口低电平采样数，
 *         用于定位"完全无应答(stage=1)"是 MCU 引脚、接线还是模块的问题。
 *         诊断用，定位后可删除。
 */
void BSP_DHT11_SelfTest(void);

#endif /* BSP_DHT11_H */
