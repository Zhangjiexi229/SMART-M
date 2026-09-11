/**
 ******************************************************************************
 * @file    bsp_ft24c256a.c
 * @brief   FT24C256A EEPROM 驱动实现 — 软件I2C（PC6=SCL, PC8=SDA开漏）
 *
 *  ▍软件I2C实现：
 *    SCL: 推挽输出，主机完全控制时钟
 *    SDA: 开漏输出 + 外部上拉，输出低电平/释放(高)，可读回从机ACK
 *
 *  ▍写流程（单页，≤64字节，不跨页）：
 *    START → 0xA0(W) → addr_hi → addr_lo → data... → STOP → 等待内部编程
 *
 *  ▍读流程（随机地址连续读）：
 *    START → 0xA0(W) → addr_hi → addr_lo → RESTART → 0xA1(R) → data...(NACK) → STOP
 *
 *  ▍写完成检测：
 *    STOP后轮询发送 START+0xA0，收到ACK表示内部编程完成（最多等10ms）
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_FT24C256A_ENABLE

#include "bsp_ft24c256a.h"
#include "bsp_delay.h"
#include "main.h"

/* ========== 软件I2C时序参数（微秒） ========== */
#define FT24C_I2C_DELAY_US     5U    /* SCL半周期，约100kHz（保守稳定） */

/* ========== 引脚操作内联 ========== */
static inline void FT24C_SCL_High(void) { HAL_GPIO_WritePin(FT24C_SCL_GPIO_Port, FT24C_SCL_Pin, GPIO_PIN_SET); }
static inline void FT24C_SCL_Low(void)  { HAL_GPIO_WritePin(FT24C_SCL_GPIO_Port, FT24C_SCL_Pin, GPIO_PIN_RESET); }
static inline void FT24C_SDA_High(void) { HAL_GPIO_WritePin(FT24C_SDA_GPIO_Port, FT24C_SDA_Pin, GPIO_PIN_SET); }   /* 开漏释放=高 */
static inline void FT24C_SDA_Low(void)  { HAL_GPIO_WritePin(FT24C_SDA_GPIO_Port, FT24C_SDA_Pin, GPIO_PIN_RESET); } /* 开漏拉低 */
static inline uint8_t FT24C_SDA_Read(void) { return (HAL_GPIO_ReadPin(FT24C_SDA_GPIO_Port, FT24C_SDA_Pin) == GPIO_PIN_SET) ? 1U : 0U; }

/* ========== GPIO 初始化 ========== */
void BSP_FT24C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIOC 时钟（PC6=SCL, PC8=SDA） */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* SCL: 开漏输出 + 上拉 */
    GPIO_InitStruct.Pin   = FT24C_SCL_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FT24C_SCL_GPIO_Port, &GPIO_InitStruct);

    /* SDA: 开漏输出 + 上拉（可读回ACK） */
    GPIO_InitStruct.Pin   = FT24C_SDA_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FT24C_SDA_GPIO_Port, &GPIO_InitStruct);

    /* 空闲状态：SCL高, SDA高 */
    FT24C_SCL_High();
    FT24C_SDA_High();
    BSP_DelayUs(10U);
}

/* ========== I2C 基础时序 ========== */

/**
 * @brief  I2C 起始信号：SCL高时 SDA高→低
 */
static void FT24C_I2C_Start(void)
{
    FT24C_SDA_High();
    FT24C_SCL_High();
    BSP_DelayUs(FT24C_I2C_DELAY_US);
    FT24C_SDA_Low();
    BSP_DelayUs(FT24C_I2C_DELAY_US);
    FT24C_SCL_Low();
    BSP_DelayUs(FT24C_I2C_DELAY_US);
}

/**
 * @brief  I2C 停止信号：SCL高时 SDA低→高
 */
static void FT24C_I2C_Stop(void)
{
    FT24C_SDA_Low();
    FT24C_SCL_High();
    BSP_DelayUs(FT24C_I2C_DELAY_US);
    FT24C_SDA_High();
    BSP_DelayUs(FT24C_I2C_DELAY_US);
}

/**
 * @brief  I2C 写一个字节并检测ACK
 * @param  byte  待发送字节
 * @retval 0=收到ACK, 1=未收到ACK(NACK)
 */
static uint8_t FT24C_I2C_WriteByte(uint8_t byte)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0U; i < 8U; i++) {
        FT24C_SCL_Low();
        BSP_DelayUs(1U);
        if (byte & 0x80U) {
            FT24C_SDA_High();
        } else {
            FT24C_SDA_Low();
        }
        byte <<= 1;
        BSP_DelayUs(FT24C_I2C_DELAY_US);
        FT24C_SCL_High();
        BSP_DelayUs(FT24C_I2C_DELAY_US);
    }

    /* 第9个时钟：读取ACK */
    FT24C_SCL_Low();
    FT24C_SDA_High();           /* 释放SDA，让从机拉低 */
    BSP_DelayUs(FT24C_I2C_DELAY_US);
    FT24C_SCL_High();
    BSP_DelayUs(FT24C_I2C_DELAY_US);
    ack = FT24C_SDA_Read();     /* 0=ACK, 1=NACK */
    FT24C_SCL_Low();
    BSP_DelayUs(1U);

    return ack;
}

/**
 * @brief  I2C 读一个字节
 * @param  ack  1=发送ACK(继续读), 0=发送NACK(最后一个字节)
 * @retval 读到的字节
 */
static uint8_t FT24C_I2C_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t byte = 0U;

    FT24C_SDA_High();   /* 释放SDA，从机驱动 */

    for (i = 0U; i < 8U; i++) {
        FT24C_SCL_Low();
        BSP_DelayUs(FT24C_I2C_DELAY_US);
        FT24C_SCL_High();
        BSP_DelayUs(1U);
        byte <<= 1;
        if (FT24C_SDA_Read()) {
            byte |= 0x01U;
        }
        BSP_DelayUs(FT24C_I2C_DELAY_US);
    }

    /* 第9个时钟：主机发送ACK/NACK */
    FT24C_SCL_Low();
    if (ack) {
        FT24C_SDA_Low();    /* ACK */
    } else {
        FT24C_SDA_High();   /* NACK */
    }
    BSP_DelayUs(FT24C_I2C_DELAY_US);
    FT24C_SCL_High();
    BSP_DelayUs(FT24C_I2C_DELAY_US);
    FT24C_SCL_Low();
    FT24C_SDA_High();       /* 释放SDA */
    BSP_DelayUs(1U);

    return byte;
}

/* ========== 写完成轮询 ========== */

/**
 * @brief  等待EEPROM内部编程完成（轮询ACK）
 * @retval 0=完成, 1=超时
 */
static uint8_t FT24C_WaitWriteComplete(void)
{
    uint32_t start = BSP_Delay_GetTickUs();
    const uint32_t timeout_us = (uint32_t)FT24C_WRITE_TIMEOUT * 1000U;

    while ((BSP_Delay_GetTickUs() - start) < timeout_us) {
        FT24C_I2C_Start();
        if (FT24C_I2C_WriteByte(FT24C_I2C_ADDR) == 0U) {
            /* 收到ACK = 编程完成 */
            FT24C_I2C_Stop();
            return 0U;
        }
        FT24C_I2C_Stop();
        BSP_DelayUs(100U);   /* 轮询间隔 */
    }
    return 1U;   /* 超时 */
}

/* ========== 单页写（不跨页，≤64字节） ========== */
static uint8_t FT24C_PageWrite(uint16_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (len == 0U) return 0U;
    if (len > FT24C_PAGE_SIZE) return 1U;
    /* 检查不跨页 */
    if (((uint32_t)addr & ~(FT24C_PAGE_SIZE - 1U)) !=
        (((uint32_t)addr + len - 1U) & ~(FT24C_PAGE_SIZE - 1U))) {
        return 1U;
    }

    FT24C_I2C_Start();
    if (FT24C_I2C_WriteByte(FT24C_I2C_ADDR) != 0U) { FT24C_I2C_Stop(); return 1U; }
    if (FT24C_I2C_WriteByte((uint8_t)(addr >> 8)) != 0U) { FT24C_I2C_Stop(); return 1U; }
    if (FT24C_I2C_WriteByte((uint8_t)(addr & 0xFF)) != 0U) { FT24C_I2C_Stop(); return 1U; }

    for (i = 0U; i < len; i++) {
        if (FT24C_I2C_WriteByte(buf[i]) != 0U) { FT24C_I2C_Stop(); return 1U; }
    }

    FT24C_I2C_Stop();

    /* 等待内部编程完成 */
    return FT24C_WaitWriteComplete();
}

/* ========== 公共 API ========== */

uint8_t BSP_FT24C_CheckReady(void)
{
    FT24C_I2C_Start();
    if (FT24C_I2C_WriteByte(FT24C_I2C_ADDR) != 0U) {
        FT24C_I2C_Stop();
        return 1U;
    }
    FT24C_I2C_Stop();
    return 0U;
}

uint8_t BSP_FT24C_Read(uint16_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if ((buf == NULL) || (len == 0U)) return 1U;
    if (((uint32_t)addr + len) > FT24C_TOTAL_SIZE) return 1U;

    /* 发送要读取的起始地址（写地址阶段） */
    FT24C_I2C_Start();
    if (FT24C_I2C_WriteByte(FT24C_I2C_ADDR) != 0U) { FT24C_I2C_Stop(); return 1U; }
    if (FT24C_I2C_WriteByte((uint8_t)(addr >> 8)) != 0U) { FT24C_I2C_Stop(); return 1U; }
    if (FT24C_I2C_WriteByte((uint8_t)(addr & 0xFF)) != 0U) { FT24C_I2C_Stop(); return 1U; }

    /* 重复起始 + 读地址 */
    FT24C_I2C_Start();
    if (FT24C_I2C_WriteByte(FT24C_I2C_ADDR | 0x01U) != 0U) { FT24C_I2C_Stop(); return 1U; }

    /* 连续读，最后一个字节发NACK */
    for (i = 0U; i < len; i++) {
        buf[i] = FT24C_I2C_ReadByte((i < (len - 1U)) ? 1U : 0U);
    }

    FT24C_I2C_Stop();
    return 0U;
}

uint8_t BSP_FT24C_Write(uint16_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t offset = 0U;

    if ((buf == NULL) || (len == 0U)) return 1U;
    if (((uint32_t)addr + len) > FT24C_TOTAL_SIZE) return 1U;

    /* 自动按页边界拆分写入 */
    while (offset < len) {
        uint16_t cur_addr = (uint16_t)((uint32_t)addr + offset);
        /* 当前页剩余空间 */
        uint32_t page_remain = FT24C_PAGE_SIZE - ((uint32_t)cur_addr & (FT24C_PAGE_SIZE - 1U));
        uint32_t chunk = (page_remain < (len - offset)) ? page_remain : (len - offset);

        if (FT24C_PageWrite(cur_addr, &buf[offset], chunk) != 0U) {
            return 1U;
        }
        offset += chunk;
    }

    return 0U;
}

#endif /* BSP_FT24C256A_ENABLE */
