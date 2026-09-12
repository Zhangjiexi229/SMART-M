/**
 ******************************************************************************
 * @file    bsp_dht11.c
 * @brief   DHT11温湿度传感器驱动实现 — PG9单总线协议 + 共用TIM2微秒计时
 *
 *  ▍延时资源分配（不冲突 / 不抢占 / 不相互影响）：
 *    1. SysTick          -> 只归 FreeRTOS（tick 调度），本驱动不依赖、不改动
 *    2. TIM6             -> 只归 HAL 时基（HAL_Delay），本驱动协议时序不使用
 *    3. TIM2             -> 所有用户微秒/毫秒延时统一走 bsp_delay.h：
 *                           BSP_DelayUs / BSP_DelayMs / BSP_Delay_GetTickUs
 *                           （自由计数器，只读 CNT，与 OLED 软件 I2C 共享安全）
 *    4. 关中断临界区      -> 从"拉高起始信号"到"读完40位"之间 __disable_irq()，
 *                           约 4~5ms/次，防止 FreeRTOS 任务抢占打断位时序
 *
 *  ▍为什么 DHT11 必须关中断（而 OLED 软件 I2C 不用）：
 *    数据位是"50us低电平 + 26~70us高电平"的脉宽信号，一旦被任务切换
 *    （上下文切换会暂停本任务数微秒~数百微秒）就会误判 0/1 或超时。
 *    本驱动在时序关键段短暂屏蔽全局中断，DMA 仍持续接收串口数据不受影响，
 *    每次读取仅关中断约 4~5ms（采集周期 2s，占比可忽略）。
 *    而 OLED 软件 I2C 是"推挽输出 + 不检测ACK"，时钟被拉长也无碍，
 *    因此只需互斥锁即可，不需要关中断。
 *
 *  时序关键参数：
 *    - 起始信号低电平：>=18ms（BSP_DelayMs，共用TIM2）
 *    - 起始信号高电平：20~40us（BSP_DelayUs）
 *    - DHT11响应低电平：80us
 *    - DHT11响应高电平：80us
 *    - 数据位起始低电平：50us
 *    - 数据0高电平：26~28us
 *    - 数据1高电平：70us
 *    - 判定阈值：高电平 >40us 判为1，否则判为0
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_DHT11_ENABLE

#include "bsp_dht11.h"
#include "bsp_delay.h"
#include "bsp_uart.h"
#include "main.h"

/* ========== 时序参数（微秒） ========== */
#define DHT11_START_LOW_MS      18U      /* 起始信号低电平 >=18ms */
#define DHT11_START_HIGH_US     30U      /* 起始信号高电平 20~40us */
#define DHT11_RESPONSE_TIMEOUT  200U     /* 响应超时阈值（us） */
#define DHT11_BIT_LOW_TIMEOUT   100U     /* 数据位低电平超时（us） */
#define DHT11_BIT_HIGH_TIMEOUT  120U     /* 数据位高电平超时（us） */
#define DHT11_BIT_THRESHOLD_US  40U      /* 数据位0/1判定阈值（us），>40=1 */

/* ========== 失败阶段记录（串口诊断用） ========== */
static uint8_t s_dht11_fail_stage = BSP_DHT11_FAIL_NONE;

/* ========== 引脚操作内联函数 ========== */
static inline void DHT11_PinLow(void)
{
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_RESET);
}

static inline void DHT11_PinHigh(void)
{
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);
}

static inline uint8_t DHT11_PinRead(void)
{
    return (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

/**
 * @brief  切换引脚为输出模式（推挽输出）
 */
static void DHT11_PinOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = DHT11_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @brief  切换引脚为输入模式（浮空输入，由外部上拉电阻拉高）
 */
static void DHT11_PinInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin  = DHT11_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);
}

/* ========== TIM2 微秒计时（只读，与 SysTick/FreeRTOS 无任何交互） ========== */

/**
 * @brief  计算从 start_tick 到当前已过的微秒数
 * @param  start_tick  起始时的 BSP_Delay_GetTickUs() 值
 * @return 已过微秒数（us）
 * @note   32位无符号减法自动处理回绕，测量窗口远小于回绕周期(约71分钟)
 */
static uint32_t DHT11_ElapsedUs(uint32_t start_tick)
{
    return (BSP_Delay_GetTickUs() - start_tick);
}

/**
 * @brief  等待引脚变为指定电平，带超时（TIM2 计时）
 * @param  level   期望电平（0=低，1=高）
 * @param  timeout 超时时间（us）
 * @return 0=成功（检测到期望电平），1=超时
 */
static uint8_t DHT11_WaitLevel(uint8_t level, uint32_t timeout)
{
    uint32_t start = BSP_Delay_GetTickUs();
    while (DHT11_PinRead() != level) {
        if (DHT11_ElapsedUs(start) >= timeout) {
            return 1U;  /* 超时 */
        }
    }
    return 0U;  /* 成功 */
}

/**
 * @brief  测量引脚高电平持续时间（us，TIM2 精确计时）
 * @return 高电平持续时间（us），超时返回 0xFFFFFFFF
 */
static uint32_t DHT11_MeasureHighUs(void)
{
    uint32_t start = BSP_Delay_GetTickUs();
    while (DHT11_PinRead() == 1U) {
        if (DHT11_ElapsedUs(start) >= DHT11_BIT_HIGH_TIMEOUT) {
            return 0xFFFFFFFFU;  /* 超时 */
        }
    }
    return DHT11_ElapsedUs(start);
}

/**
 * @brief  DHT11 初始化
 * @note   TIM2 自由计数器由 BSP_Delay_Init() 统一初始化（APP_Init 内，调度器前）
 */
void BSP_DHT11_Init(void)
{
    /* GPIO 引脚由 MX_GPIO_Init() 配置为输出，本函数仅作占位说明 */
}

/**
 * @brief  读取一次 DHT11 温湿度数据（阻塞式，约 20ms+）
 * @param  data  输出参数，读取到的温湿度数据
 * @return BSP_DHT11_OK=成功，其他=失败
 * @note   调用间隔应 >=1秒，建议2秒；读取失败时 data 内容不变
 */
BSP_DHT11_Status_t BSP_DHT11_Read(BSP_DHT11_Data_t *data)
{
    uint8_t  buf[5];
    uint8_t  i;
    uint8_t  bit;
    uint32_t high_us;
    uint8_t  checksum;
    BSP_DHT11_Status_t status = BSP_DHT11_OK;

    if (data == NULL) {
        return BSP_DHT11_ERR_TIMEOUT;
    }

    s_dht11_fail_stage = BSP_DHT11_FAIL_NONE;   /* 每次读取前复位失败阶段 */

    /* ---- 步骤0：确保引脚处于输出高电平（空闲状态） ---- */
    DHT11_PinOutput();
    DHT11_PinHigh();
    BSP_DelayUs(100U);  /* 短暂稳定 */

    /* ---- 步骤1：主机发送起始信号（18ms 低电平，共用 TIM2 忙等） ---- */
    DHT11_PinLow();
    BSP_DelayMs(DHT11_START_LOW_MS);

    /* ================================================================
     *  时序关键段开始：关中断，禁止任务抢占
     *  从拉高起始信号到读完 40 位，约 4~5ms，期间 DMA 仍持续收串口数据
     * ================================================================ */
    __disable_irq();

    DHT11_PinHigh();                 /* 拉高总线 */
    BSP_DelayUs(DHT11_START_HIGH_US); /* 保持高电平 20~40us */

    /* ---- 步骤2：切换为输入模式，等待DHT11响应 ---- */
    DHT11_PinInput();

    if (DHT11_WaitLevel(0U, DHT11_RESPONSE_TIMEOUT) != 0U) {
        s_dht11_fail_stage = BSP_DHT11_FAIL_RESP_LOW;   /* 完全没回应 */
        status = BSP_DHT11_ERR_TIMEOUT;
        goto exit;
    }
    if (DHT11_WaitLevel(1U, DHT11_RESPONSE_TIMEOUT) != 0U) {
        s_dht11_fail_stage = BSP_DHT11_FAIL_RESP_HIGH;
        status = BSP_DHT11_ERR_TIMEOUT;
        goto exit;
    }
    if (DHT11_WaitLevel(0U, DHT11_RESPONSE_TIMEOUT) != 0U) {
        s_dht11_fail_stage = BSP_DHT11_FAIL_RESP_END;
        status = BSP_DHT11_ERR_TIMEOUT;
        goto exit;
    }

    /* ---- 步骤3：读取40位数据（5字节） ---- */
    for (i = 0U; i < 5U; i++) {
        buf[i] = 0U;
        for (bit = 0U; bit < 8U; bit++) {
            if (DHT11_WaitLevel(1U, DHT11_BIT_LOW_TIMEOUT) != 0U) {
                s_dht11_fail_stage = BSP_DHT11_FAIL_BIT_LOW;
                status = BSP_DHT11_ERR_TIMEOUT;
                goto exit;
            }
            high_us = DHT11_MeasureHighUs();
            if (high_us == 0xFFFFFFFFU) {
                s_dht11_fail_stage = BSP_DHT11_FAIL_BIT_HIGH;
                status = BSP_DHT11_ERR_TIMEOUT;
                goto exit;
            }
            buf[i] <<= 1;
            if (high_us > DHT11_BIT_THRESHOLD_US) {
                buf[i] |= 0x01U;  /* 高电平 >40us，判定为1 */
            }
        }
    }

exit:
    /* 恢复总线为输出高电平（空闲状态） */
    DHT11_PinOutput();
    DHT11_PinHigh();
    /* ================================================================
     *  时序关键段结束：重新开中断
     * ================================================================ */
    __enable_irq();

    if (status != BSP_DHT11_OK) {
        return status;
    }

    /* ---- 步骤5：校验和验证 ---- */
    checksum = (uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]);
    if (checksum != buf[4]) {
        s_dht11_fail_stage = BSP_DHT11_FAIL_CHECKSUM;
        return BSP_DHT11_ERR_CHECKSUM;
    }

    /* ---- 步骤6：填充输出数据 ---- */
    data->humidity_int    = buf[0];
    data->humidity_dec    = buf[1];
    data->temperature_int = buf[2];
    data->temperature_dec = buf[3];

    return BSP_DHT11_OK;
}

/**
 * @brief  获取最近一次读取的失败阶段（串口诊断用）
 * @return 失败阶段枚举；读取成功时为 BSP_DHT11_FAIL_NONE
 */
BSP_DHT11_FailStage_t BSP_DHT11_GetLastFailStage(void)
{
    return (BSP_DHT11_FailStage_t)s_dht11_fail_stage;
}

/**
 * @brief  DHT11 硬件自检 — 定位"完全无应答(stage=1)"的具体硬件原因
 * @note   串口打印 3 行诊断，调用一次约 25ms：
 *         ① GPIO 环回：输出高/低再读回，验证 PG9 引脚本身是否正常（期望 1,0）
 *         ② 空闲总线电平：正常应被上拉为 1；读到 0 说明被外部拉低（接线错误）
 *         ③ 应答窗口低电平采样数：
 *              0  = 传感器完全无回应（查供电/数据线是否接 PG9/模块是否损坏）
 *              >0 = 传感器有回应但时序不对（查上拉电阻/线长/电源纹波）
 *         诊断用，定位后可删除本函数及其调用。
 */
void BSP_DHT11_SelfTest(void)
{
    uint32_t low_count = 0U;
    uint32_t start;

    BSP_UART1_Printf("[DHT11-DIAG] ====== SelfTest ======\r\n");

    /* ⓪ TIM2 计时自检：延时1000us前后各读一次CNT，差值应≈1000（证明定时器在跑且1us/tick，无需CubeMX配置） */
    {
        uint32_t t0 = BSP_Delay_GetTickUs();
        BSP_DelayUs(1000U);
        uint32_t t1 = BSP_Delay_GetTickUs();
        BSP_UART1_Printf("[DHT11-DIAG] TIM2 cnt before=%lu after=%lu delta=%lu (期望≈1000)\r\n",
                         (unsigned long)t0, (unsigned long)t1, (unsigned long)(t1 - t0));
    }

    /* ① GPIO 环回：输出高/低后读回，验证 PG9 输出与读回通路 */
    DHT11_PinOutput();
    DHT11_PinHigh();
    BSP_DelayUs(10U);
    BSP_UART1_Printf("[DHT11-DIAG] loopback: outH read=%u",
                     (unsigned)DHT11_PinRead());
    DHT11_PinLow();
    BSP_DelayUs(10U);
    BSP_UART1_Printf(" | outL read=%u  (期望 1,0)\r\n",
                     (unsigned)DHT11_PinRead());

    /* ② 空闲电平：总线应被上拉为高电平 */
    DHT11_PinHigh();
    BSP_DelayUs(200U);
    BSP_UART1_Printf("[DHT11-DIAG] idle bus=%u  (期望1；0=被外部拉低/接线错误)\r\n",
                     (unsigned)DHT11_PinRead());

    /* ③ 发一次完整起始信号，统计应答窗口内总线被拉低的采样次数 */
    DHT11_PinLow();
    BSP_DelayMs(DHT11_START_LOW_MS);
    __disable_irq();
    DHT11_PinHigh();
    BSP_DelayUs(DHT11_START_HIGH_US);
    DHT11_PinInput();
    start = BSP_Delay_GetTickUs();
    while ((BSP_Delay_GetTickUs() - start) < (DHT11_RESPONSE_TIMEOUT + 100U)) {
        if (DHT11_PinRead() == 0U) {
            low_count++;
        }
    }
    DHT11_PinOutput();
    DHT11_PinHigh();
    __enable_irq();

    BSP_UART1_Printf("[DHT11-DIAG] response low samples=%lu /300us  "
                     "(0=无回应[查供电/接线/模块]；>0=有回应[查上拉/线长])\r\n",
                     (unsigned long)low_count);
    BSP_UART1_Printf("[DHT11-DIAG] ====== end ======\r\n");
}

#endif /* BSP_DHT11_ENABLE */
