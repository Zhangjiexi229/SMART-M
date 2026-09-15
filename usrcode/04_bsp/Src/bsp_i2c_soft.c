/**
 ******************************************************************************
 * @file    bsp_i2c_soft.c
 * @brief   共享软件I2C总线驱动实现 — PB6=SCL, PB7=SDA
 *
 *  ▍软件I2C实现（与 bsp_at24c02.c 同构）：
 *    SCL: 推挽输出，主机完全控制时钟
 *    SDA: 动态切换 — 写数据时推挽输出（主动驱动高低电平，不依赖外部上拉），
 *         读ACK/数据时切换为输入上拉（纯高阻释放总线，让从机驱动）
 *
 *  ▍并发保护：
 *    每个事务（START...STOP）用 __disable_irq()/__enable_irq() 临界区包裹。
 *    I2C 单事务仅数十~数百微秒，关中断可接受；
 *    SHT30 的 20ms 测量等待由 bsp_sht30.c 放在临界区之外。
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_I2C_SOFT_ENABLE

#include "bsp_i2c_soft.h"
#include "bsp_delay.h"
#include "main.h"

/* ========== 软件I2C时序参数（微秒） ========== */
#define I2C_SOFT_DELAY_US       5U    /* SCL半周期，约100kHz */

/* ========== 引脚操作内联 ========== */
static inline void I2C_SOFT_SCL_High(void) { HAL_GPIO_WritePin(I2C_SOFT_SCL_GPIO_Port, I2C_SOFT_SCL_Pin, GPIO_PIN_SET); }
static inline void I2C_SOFT_SCL_Low(void)  { HAL_GPIO_WritePin(I2C_SOFT_SCL_GPIO_Port, I2C_SOFT_SCL_Pin, GPIO_PIN_RESET); }
static inline void I2C_SOFT_SDA_High(void) { HAL_GPIO_WritePin(I2C_SOFT_SDA_GPIO_Port, I2C_SOFT_SDA_Pin, GPIO_PIN_SET); }
static inline void I2C_SOFT_SDA_Low(void)  { HAL_GPIO_WritePin(I2C_SOFT_SDA_GPIO_Port, I2C_SOFT_SDA_Pin, GPIO_PIN_RESET); }
static inline uint8_t I2C_SOFT_SDA_Read(void) { return (HAL_GPIO_ReadPin(I2C_SOFT_SDA_GPIO_Port, I2C_SOFT_SDA_Pin) == GPIO_PIN_SET) ? 1U : 0U; }

/**
 * @brief  SDA切换为推挽输出模式（写数据/Start/Stop/主机ACK时用）
 */
static inline void I2C_SOFT_SDA_Output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = I2C_SOFT_SDA_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C_SOFT_SDA_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @brief  SDA切换为输入上拉模式（读ACK/读数据时用，释放总线让从机驱动）
 */
static inline void I2C_SOFT_SDA_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = I2C_SOFT_SDA_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(I2C_SOFT_SDA_GPIO_Port, &GPIO_InitStruct);
}

/* ========== GPIO 初始化 ========== */
void BSP_I2C_Soft_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* SCL: 推挽输出 */
    GPIO_InitStruct.Pin   = I2C_SOFT_SCL_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C_SOFT_SCL_GPIO_Port, &GPIO_InitStruct);

    /* SDA: 初始推挽输出（通信过程中动态切换） */
    I2C_SOFT_SDA_Output();

    /* 空闲状态：SCL高, SDA高 */
    I2C_SOFT_SCL_High();
    I2C_SOFT_SDA_High();
    BSP_DelayUs(10U);
}

/* ========== I2C 基础时序 ========== */

/**
 * @brief  I2C 起始信号：SCL高时 SDA高→低
 */
static void I2C_SOFT_Start(void)
{
    I2C_SOFT_SDA_Output();
    I2C_SOFT_SDA_High();
    I2C_SOFT_SCL_High();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    I2C_SOFT_SDA_Low();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    I2C_SOFT_SCL_Low();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
}

/**
 * @brief  I2C 停止信号：SCL高时 SDA低→高
 */
static void I2C_SOFT_Stop(void)
{
    I2C_SOFT_SDA_Output();
    I2C_SOFT_SDA_Low();
    I2C_SOFT_SCL_Low();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    I2C_SOFT_SCL_High();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    I2C_SOFT_SDA_High();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
}

/**
 * @brief  I2C 写一个字节并检测ACK
 * @param  byte  待发送字节
 * @retval 0=收到ACK, 1=未收到ACK(NACK)
 */
static uint8_t I2C_SOFT_WriteByte(uint8_t byte)
{
    uint8_t i;
    uint8_t ack;

    I2C_SOFT_SDA_Output();

    for (i = 0U; i < 8U; i++) {
        I2C_SOFT_SCL_Low();
        BSP_DelayUs(1U);
        if (byte & 0x80U) {
            I2C_SOFT_SDA_High();
        } else {
            I2C_SOFT_SDA_Low();
        }
        byte <<= 1;
        BSP_DelayUs(I2C_SOFT_DELAY_US);
        I2C_SOFT_SCL_High();
        BSP_DelayUs(I2C_SOFT_DELAY_US);
    }

    /* 第9个时钟：读取ACK */
    I2C_SOFT_SCL_Low();
    I2C_SOFT_SDA_Input();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    I2C_SOFT_SCL_High();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    ack = I2C_SOFT_SDA_Read();
    I2C_SOFT_SCL_Low();
    BSP_DelayUs(1U);

    return ack;
}

/**
 * @brief  I2C 读一个字节
 * @param  ack  1=发送ACK(继续读), 0=发送NACK(最后一个字节)
 * @retval 读到的字节
 */
static uint8_t I2C_SOFT_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t byte = 0U;

    I2C_SOFT_SDA_Input();

    for (i = 0U; i < 8U; i++) {
        I2C_SOFT_SCL_Low();
        BSP_DelayUs(I2C_SOFT_DELAY_US);
        I2C_SOFT_SCL_High();
        BSP_DelayUs(I2C_SOFT_DELAY_US);
        byte <<= 1;
        if (I2C_SOFT_SDA_Read()) {
            byte |= 0x01U;
        }
        BSP_DelayUs(I2C_SOFT_DELAY_US);
    }

    /* 第9个时钟：主机发送ACK/NACK */
    /* 先拉低 SCL，再切换 SDA 为输出（避免 SCL 高时 SDA 变化产生 Start/Stop） */
    I2C_SOFT_SCL_Low();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    I2C_SOFT_SDA_Output();
    if (ack) {
        I2C_SOFT_SDA_Low();
    } else {
        I2C_SOFT_SDA_High();
    }
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    I2C_SOFT_SCL_High();
    BSP_DelayUs(I2C_SOFT_DELAY_US);
    I2C_SOFT_SCL_Low();
    I2C_SOFT_SDA_High();
    BSP_DelayUs(1U);

    return byte;
}

/* ========== 公共 API ========== */

/**
 * @brief  I2C总线恢复：9个SCL脉冲 + 停止位
 * @note   先把SCL/SDA重配为开漏输出（外加上拉），逐脉冲拉低/拉高SCL
 *         释放从机对SDA的占用，最后SDA低->高产生停止条件，再恢复
 *         原有的推挽SCL/SDA模式。SCL脚在GPIO配置期间一直保持低，
 *         不会因悬浮产生伪时钟。
 */
void BSP_I2C_Soft_Recover(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 1. SCL/SDA 重配为开漏输出（释放总线，从机可驱动） */
    GPIO_InitStruct.Pin   = I2C_SOFT_SCL_Pin | I2C_SOFT_SDA_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C_SOFT_SCL_GPIO_Port, &GPIO_InitStruct);

    /* 2. SDA 拉高（空闲），SCL 初始高 */
    HAL_GPIO_WritePin(I2C_SOFT_SDA_GPIO_Port, I2C_SOFT_SDA_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(I2C_SOFT_SCL_GPIO_Port, I2C_SOFT_SCL_Pin, GPIO_PIN_SET);
    BSP_DelayUs(10U);

    /* 3. 产生9个时钟脉冲 */
    for (i = 0U; i < 9U; i++) {
        HAL_GPIO_WritePin(I2C_SOFT_SCL_GPIO_Port, I2C_SOFT_SCL_Pin, GPIO_PIN_RESET);
        BSP_DelayUs(10U);
        HAL_GPIO_WritePin(I2C_SOFT_SCL_GPIO_Port, I2C_SOFT_SCL_Pin, GPIO_PIN_SET);
        BSP_DelayUs(10U);
    }

    /* 4. 产生停止条件：SCL高期间 SDA 低->高 */
    HAL_GPIO_WritePin(I2C_SOFT_SDA_GPIO_Port, I2C_SOFT_SDA_Pin, GPIO_PIN_RESET);
    BSP_DelayUs(10U);
    HAL_GPIO_WritePin(I2C_SOFT_SCL_GPIO_Port, I2C_SOFT_SCL_Pin, GPIO_PIN_SET);
    BSP_DelayUs(10U);
    HAL_GPIO_WritePin(I2C_SOFT_SDA_GPIO_Port, I2C_SOFT_SDA_Pin, GPIO_PIN_SET);
    BSP_DelayUs(10U);

    /* 5. 恢复原有工作模式（SCL推挽 + SDA推挽输出），等效重新初始化 */
    BSP_I2C_Soft_Init();
}

uint8_t BSP_I2C_Soft_Probe(uint8_t dev_addr)
{
    uint8_t ack;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    I2C_SOFT_Start();
    ack = I2C_SOFT_WriteByte((uint8_t)((dev_addr << 1U) | 0x00U));
    I2C_SOFT_Stop();

    __set_PRIMASK(primask);
    return ack;
}

uint8_t BSP_I2C_Soft_WriteRegs(uint8_t dev_addr, uint8_t reg,
                               const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint32_t primask;

    if ((buf == NULL) || (len == 0U)) {
        return 1U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    I2C_SOFT_Start();
    if (I2C_SOFT_WriteByte((uint8_t)((dev_addr << 1U) | 0x00U)) != 0U) { I2C_SOFT_Stop(); __set_PRIMASK(primask); return 1U; }
    if (I2C_SOFT_WriteByte(reg) != 0U)                                 { I2C_SOFT_Stop(); __set_PRIMASK(primask); return 1U; }
    for (i = 0U; i < len; i++) {
        if (I2C_SOFT_WriteByte(buf[i]) != 0U)                          { I2C_SOFT_Stop(); __set_PRIMASK(primask); return 1U; }
    }
    I2C_SOFT_Stop();

    __set_PRIMASK(primask);
    return 0U;
}

uint8_t BSP_I2C_Soft_WriteReg(uint8_t dev_addr, uint8_t reg, uint8_t data)
{
    return BSP_I2C_Soft_WriteRegs(dev_addr, reg, &data, 1U);
}

uint8_t BSP_I2C_Soft_ReadRegs(uint8_t dev_addr, uint8_t reg,
                              uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint32_t primask;

    if ((buf == NULL) || (len == 0U)) {
        return 1U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    /* 写寄存器地址（写方向） */
    I2C_SOFT_Start();
    if (I2C_SOFT_WriteByte((uint8_t)((dev_addr << 1U) | 0x00U)) != 0U) { I2C_SOFT_Stop(); __set_PRIMASK(primask); return 1U; }
    if (I2C_SOFT_WriteByte(reg) != 0U)                                 { I2C_SOFT_Stop(); __set_PRIMASK(primask); return 1U; }

    /* 重复起始 + 读方向 */
    I2C_SOFT_Start();
    if (I2C_SOFT_WriteByte((uint8_t)((dev_addr << 1U) | 0x01U)) != 0U) { I2C_SOFT_Stop(); __set_PRIMASK(primask); return 1U; }

    /* 连续读，最后一个字节发NACK */
    for (i = 0U; i < len; i++) {
        buf[i] = I2C_SOFT_ReadByte((i < (len - 1U)) ? 1U : 0U);
    }

    I2C_SOFT_Stop();

    __set_PRIMASK(primask);
    return 0U;
}

uint8_t BSP_I2C_Soft_Read(uint8_t dev_addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint32_t primask;

    if ((buf == NULL) || (len == 0U)) {
        return 1U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    /* 重复起始 + 读方向（无寄存器地址，SHT30 官方时序） */
    I2C_SOFT_Start();
    if (I2C_SOFT_WriteByte((uint8_t)((dev_addr << 1U) | 0x01U)) != 0U) { I2C_SOFT_Stop(); __set_PRIMASK(primask); return 1U; }

    /* 连续读，最后一个字节发NACK */
    for (i = 0U; i < len; i++) {
        buf[i] = I2C_SOFT_ReadByte((i < (len - 1U)) ? 1U : 0U);
    }

    I2C_SOFT_Stop();

    __set_PRIMASK(primask);
    return 0U;
}

#endif /* BSP_I2C_SOFT_ENABLE */
