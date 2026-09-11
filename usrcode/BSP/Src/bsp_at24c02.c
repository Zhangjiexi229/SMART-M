/**
 ******************************************************************************
 * @file    bsp_at24c02.c
 * @brief   AT24C02 EEPROM 驱动实现 — 软件I2C（PB8=SCL, PB9=SDA）
 *
 *  ▍软件I2C实现：
 *    SCL: 推挽输出，主机完全控制时钟
 *    SDA: 动态切换 — 写数据时推挽输出（主动驱动高低电平，不依赖外部上拉），
 *         读ACK/数据时切换为输入上拉（纯高阻释放总线，让从机驱动）
 *
 *  ▍写流程（单页，≤8字节，不跨页）：
 *    START → 0xA0(W) → addr → data... → STOP → 等待内部编程
 *
 *  ▍读流程（随机地址连续读）：
 *    START → 0xA0(W) → addr → RESTART → 0xA1(R) → data...(NACK) → STOP
 *
 *  ▍写完成检测：
 *    STOP后轮询发送 START+0xA0，收到ACK表示内部编程完成（最多等10ms）
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_AT24C02_ENABLE

#include "bsp_at24c02.h"
#include "bsp_delay.h"
#include "main.h"

/* ========== 软件I2C时序参数（微秒） ========== */
#define AT24C02_I2C_DELAY_US     5U    /* SCL半周期，约100kHz（推挽输出不依赖外部上拉，可跑满100kHz） */

/* ========== 引脚操作内联 ========== */
static inline void AT24C02_SCL_High(void) { HAL_GPIO_WritePin(AT24C02_SCL_GPIO_Port, AT24C02_SCL_Pin, GPIO_PIN_SET); }
static inline void AT24C02_SCL_Low(void)  { HAL_GPIO_WritePin(AT24C02_SCL_GPIO_Port, AT24C02_SCL_Pin, GPIO_PIN_RESET); }
static inline void AT24C02_SDA_High(void) { HAL_GPIO_WritePin(AT24C02_SDA_GPIO_Port, AT24C02_SDA_Pin, GPIO_PIN_SET); }
static inline void AT24C02_SDA_Low(void)  { HAL_GPIO_WritePin(AT24C02_SDA_GPIO_Port, AT24C02_SDA_Pin, GPIO_PIN_RESET); }
static inline uint8_t AT24C02_SDA_Read(void) { return (HAL_GPIO_ReadPin(AT24C02_SDA_GPIO_Port, AT24C02_SDA_Pin) == GPIO_PIN_SET) ? 1U : 0U; }

/**
 * @brief  SDA切换为推挽输出模式（写数据/Start/Stop/主机ACK时用，主动驱动高低电平）
 */
static inline void AT24C02_SDA_Output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = AT24C02_SDA_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;   /* 推挽输出，写1主动拉高，不依赖外部上拉 */
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(AT24C02_SDA_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @brief  SDA切换为输入上拉模式（读ACK/读数据时用，释放总线让从机驱动）
 */
static inline void AT24C02_SDA_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = AT24C02_SDA_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;       /* 输入模式，纯高阻，从机驱动更轻松 */
    GPIO_InitStruct.Pull  = GPIO_PULLUP;           /* 内部上拉，从机释放时保持高电平 */
    HAL_GPIO_Init(AT24C02_SDA_GPIO_Port, &GPIO_InitStruct);
}

/* ========== GPIO 初始化 ========== */
void BSP_AT24C02_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIOB 时钟（PB8=SCL, PB9=SDA） */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* SCL: 推挽输出（主机独占时钟线，推挽输出边沿更陡更可靠） */
    GPIO_InitStruct.Pin   = AT24C02_SCL_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(AT24C02_SCL_GPIO_Port, &GPIO_InitStruct);

    /* SDA: 初始推挽输出（通信过程中动态切换输入/输出） */
    AT24C02_SDA_Output();

    /* 空闲状态：SCL高, SDA高 */
    AT24C02_SCL_High();
    AT24C02_SDA_High();
    BSP_DelayUs(10U);
}

/* ========== I2C 基础时序 ========== */

/**
 * @brief  I2C 起始信号：SCL高时 SDA高→低
 */
static void AT24C02_I2C_Start(void)
{
    AT24C02_SDA_Output();   /* SDA推挽输出，主动驱动 */
    AT24C02_SDA_High();     // SDA高
    AT24C02_SCL_High();     // SCL高
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
    AT24C02_SDA_Low();      // SDA低，起始信号
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
    AT24C02_SCL_Low();      // SCL低
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
}

/**
 * @brief  I2C 停止信号：SCL高时 SDA低→高
 */
static void AT24C02_I2C_Stop(void)
{
    AT24C02_SDA_Output();   /* SDA推挽输出，主动驱动 */
    AT24C02_SDA_Low();      // SDA低
    AT24C02_SCL_Low();      // SCL低
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
    AT24C02_SCL_High();     // SCL高
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
    AT24C02_SDA_High();     // SDA高，停止信号
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
}

/**
 * @brief  I2C 写一个字节并检测ACK
 * @param  byte  待发送字节
 * @retval 0=收到ACK, 1=未收到ACK(NACK)
 */
static uint8_t AT24C02_I2C_WriteByte(uint8_t byte)
{
    uint8_t i;
    uint8_t ack;  // 0=ACK, 1=NACK

    AT24C02_SDA_Output();   /* SDA推挽输出，主动驱动数据位 */

    for (i = 0U; i < 8U; i++) {
        AT24C02_SCL_Low();                          // SCL低，准备发送数据
        BSP_DelayUs(1U);
        if (byte & 0x80U) {
            AT24C02_SDA_High();                     // 推挽输出高，主动驱动，不依赖外部上拉
        } else {
            AT24C02_SDA_Low();
        }
        byte <<= 1;
        BSP_DelayUs(AT24C02_I2C_DELAY_US);         // 数据建立时间
        AT24C02_SCL_High();                         // SCL高，从机采样数据
        BSP_DelayUs(AT24C02_I2C_DELAY_US);
    }

    /* 第9个时钟：读取ACK — SDA切换为输入，释放总线让从机拉低 */
    AT24C02_SCL_Low();
    AT24C02_SDA_Input();                            /* SDA输入上拉，释放总线 */
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
    AT24C02_SCL_High();
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
    ack = AT24C02_SDA_Read();                       /* 0=ACK, 1=NACK */
    AT24C02_SCL_Low();
    BSP_DelayUs(1U);

    return ack;
}

/**
 * @brief  I2C 读一个字节
 * @param  ack  1=发送ACK(继续读), 0=发送NACK(最后一个字节)
 * @retval 读到的字节
 */
static uint8_t AT24C02_I2C_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t byte = 0U;

    AT24C02_SDA_Input();    /* SDA输入上拉，释放总线让从机驱动数据 */

    for (i = 0U; i < 8U; i++) {
        AT24C02_SCL_Low();
        BSP_DelayUs(AT24C02_I2C_DELAY_US);
        AT24C02_SCL_High();                         /* SCL高，从机输出数据 */
        BSP_DelayUs(AT24C02_I2C_DELAY_US);         /* 等待从机数据稳定 */
        byte <<= 1;
        if (AT24C02_SDA_Read()) {
            byte |= 0x01U;
        }
        BSP_DelayUs(AT24C02_I2C_DELAY_US);
    }

    /* 第9个时钟：主机发送ACK/NACK — SDA切换为推挽输出主动驱动 */
    AT24C02_SDA_Output();
    AT24C02_SCL_Low();
    if (ack) {
        AT24C02_SDA_Low();    /* ACK, 0=ACK */
    } else {
        AT24C02_SDA_High();   /* NACK, 1=NACK */
    }
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
    AT24C02_SCL_High();
    BSP_DelayUs(AT24C02_I2C_DELAY_US);
    AT24C02_SCL_Low();
    AT24C02_SDA_High();       /* 释放SDA */
    BSP_DelayUs(1U);

    return byte;
}

/* ========== 写完成轮询 ========== */

/**
 * @brief  等待EEPROM内部编程完成（轮询ACK）
 * @retval 0=完成, 1=超时
 */
static uint8_t AT24C02_WaitWriteComplete(void)
{
    uint32_t start = BSP_Delay_GetTickUs();
    const uint32_t timeout_us = (uint32_t)AT24C02_WRITE_TIMEOUT * 1000U;

    while ((BSP_Delay_GetTickUs() - start) < timeout_us) { // 轮询超时
        AT24C02_I2C_Start(); // 发送起始信号
        if (AT24C02_I2C_WriteByte(AT24C02_I2C_ADDR) == 0U) { // 发送器件地址，检查ACK
            /* 收到ACK = 编程完成 */
            AT24C02_I2C_Stop();
            return 0U;
        }
        AT24C02_I2C_Stop(); // 发送停止信号
        BSP_DelayUs(100U);   /* 轮询间隔 */
    }
    return 1U;   /* 超时 */
}

/* ========== 单页写（不跨页，≤8字节） ========== */
static uint8_t AT24C02_PageWrite(uint16_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (len == 0U) return 0U;
    if (len > AT24C02_PAGE_SIZE) return 1U;
    /* 检查不跨页 */
    if (((uint32_t)addr & ~(AT24C02_PAGE_SIZE - 1U)) !=
        (((uint32_t)addr + len - 1U) & ~(AT24C02_PAGE_SIZE - 1U))) {
        return 1U;
    }

    AT24C02_I2C_Start();
    if (AT24C02_I2C_WriteByte(AT24C02_I2C_ADDR) != 0U) { AT24C02_I2C_Stop(); return 1U; }
    if (AT24C02_I2C_WriteByte((uint8_t)(addr & 0xFF)) != 0U) { AT24C02_I2C_Stop(); return 1U; }

    for (i = 0U; i < len; i++) {
        if (AT24C02_I2C_WriteByte(buf[i]) != 0U) { AT24C02_I2C_Stop(); return 1U; }
    }

    AT24C02_I2C_Stop();

    /* 等待内部编程完成 */
    return AT24C02_WaitWriteComplete();
}

/* ========== 公共 API ========== */

uint8_t BSP_AT24C02_CheckReady(void)
{
    AT24C02_I2C_Start();
    if (AT24C02_I2C_WriteByte(AT24C02_I2C_ADDR) != 0U) {
        AT24C02_I2C_Stop();
        return 1U;
    }
    AT24C02_I2C_Stop();
    return 0U;
}

uint8_t BSP_AT24C02_CheckBusLevel(void)
{
    uint8_t level = 0U;

    /* SCL推挽输出高 */
    AT24C02_SCL_High();
    /* SDA切换为输入上拉，释放总线 */
    AT24C02_SDA_Input();
    BSP_DelayUs(50U);  /* 等待电平稳定 */

    if (AT24C02_SDA_Read()) level |= 0x01U;  /* bit0: SDA */
    if (HAL_GPIO_ReadPin(AT24C02_SCL_GPIO_Port, AT24C02_SCL_Pin) == GPIO_PIN_SET) level |= 0x02U;

    /* 恢复SDA为推挽输出高（空闲状态） */
    AT24C02_SDA_Output();
    AT24C02_SDA_High();

    return level;
}

uint8_t BSP_AT24C02_Read(uint16_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if ((buf == NULL) || (len == 0U)) return 1U;
    if (((uint32_t)addr + len) > AT24C02_TOTAL_SIZE) return 1U;

    /* 发送要读取的起始地址（写地址阶段） */
    AT24C02_I2C_Start();
    if (AT24C02_I2C_WriteByte(AT24C02_I2C_ADDR) != 0U) { AT24C02_I2C_Stop(); return 1U; }
    if (AT24C02_I2C_WriteByte((uint8_t)(addr & 0xFF)) != 0U) { AT24C02_I2C_Stop(); return 1U; }

    /* 重复起始 + 读地址 */
    AT24C02_I2C_Start();
    if (AT24C02_I2C_WriteByte(AT24C02_I2C_ADDR | 0x01U) != 0U) { AT24C02_I2C_Stop(); return 1U; }

    /* 连续读，最后一个字节发NACK */
    for (i = 0U; i < len; i++) {
        buf[i] = AT24C02_I2C_ReadByte((i < (len - 1U)) ? 1U : 0U);
    }

    AT24C02_I2C_Stop();
    return 0U;
}

uint8_t BSP_AT24C02_Write(uint16_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t offset = 0U;

    if ((buf == NULL) || (len == 0U)) return 1U;
    if (((uint32_t)addr + len) > AT24C02_TOTAL_SIZE) return 1U;

    /* 自动按页边界拆分写入 */
    while (offset < len) {
        uint16_t cur_addr = (uint16_t)((uint32_t)addr + offset);
        /* 当前页剩余空间 */
        uint32_t page_remain = AT24C02_PAGE_SIZE - ((uint32_t)cur_addr & (AT24C02_PAGE_SIZE - 1U));
        uint32_t chunk = (page_remain < (len - offset)) ? page_remain : (len - offset);

        if (AT24C02_PageWrite(cur_addr, &buf[offset], chunk) != 0U) {
            return 1U;
        }
        offset += chunk;
    }

    return 0U;
}

#endif /* BSP_AT24C02_ENABLE */
