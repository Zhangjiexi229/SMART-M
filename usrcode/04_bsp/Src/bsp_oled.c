/**
 ******************************************************************************
 * @file    bsp_oled.c
 * @brief   OLED驱动实现 — 0.96英寸 SSD1306，PD6/PD7 软件I2C + TIM2微秒延时
 *
 *  ▍软件I2C实现（不检测ACK，推挽输出）：
 *    起始：SDA高->低（SCL高时）  停止：SDA低->高（SCL高时）
 *    写字节：MSB在前，每 bit SCL低->置SDA->SCL高->SCL低
 *    不检测ACK：SSD1306为总线上唯一从机，写操作稳定，省略ACK节省代码
 *
 *  ▍延时：SCL半周期 2us（约250kHz有效速率），走 BSP_DelayUs(TIM2)
 *
 *  ▍字库：6x8 ASCII（0x20~0x7E），每个字符6字节，列优先（每字节一列8像素）
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_OLED_ENABLE

#include "bsp_oled.h"
#include "bsp_delay.h"
#include "bsp_uart.h"
#include "main.h"

/* ========== 软件I2C引脚操作 ========== */
static inline void OLED_SCL_High(void) { HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_SET); }
static inline void OLED_SCL_Low(void)  { HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_RESET); }
static inline void OLED_SDA_High(void) { HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_SET); }
static inline void OLED_SDA_Low(void)  { HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_RESET); }

#define OLED_I2C_DELAY_US   2U   /* SCL半周期，约250kHz */

/**
 * @brief  I2C 起始信号
 */
static void OLED_I2C_Start(void)
{
    OLED_SDA_High();
    OLED_SCL_High();
    BSP_DelayUs(OLED_I2C_DELAY_US);
    OLED_SDA_Low();
    BSP_DelayUs(OLED_I2C_DELAY_US);
    OLED_SCL_Low();
    BSP_DelayUs(OLED_I2C_DELAY_US);
}

/**
 * @brief  I2C 停止信号
 */
static void OLED_I2C_Stop(void)
{
    OLED_SDA_Low();
    OLED_SCL_High();
    BSP_DelayUs(OLED_I2C_DELAY_US);
    OLED_SDA_High();
    BSP_DelayUs(OLED_I2C_DELAY_US);
}

/**
 * @brief  I2C 写一个字节（产生第9个ACK时钟，但不检测ACK值）
 * @param  byte  待发送字节
 * @note   即使不检测ACK，也必须产生第9个时钟，否则SSD1306的I2C状态机
 *         不会确认数据，导致所有命令/数据都不生效。
 */
static void OLED_I2C_WriteByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        OLED_SCL_Low();
        BSP_DelayUs(1U);
        if (byte & 0x80U) {
            OLED_SDA_High();
        } else {
            OLED_SDA_Low();
        }
        byte <<= 1;
        BSP_DelayUs(1U);
        OLED_SCL_High();
        BSP_DelayUs(OLED_I2C_DELAY_US);
    }
    /* 第9个时钟：ACK周期 — 主机释放SDA，从机拉低表示ACK，此处不检测 */
    OLED_SCL_Low();
    OLED_SDA_High();          /* 释放SDA，让从机可以拉低 */
    BSP_DelayUs(1U);
    OLED_SCL_High();          /* 第9个时钟高电平 */
    BSP_DelayUs(OLED_I2C_DELAY_US);
    OLED_SCL_Low();           /* 第9个时钟低电平，结束ACK周期 */
    BSP_DelayUs(1U);
}

/**
 * @brief  向 SSD1306 写命令
 * @param  cmd  命令字节
 */
static void OLED_WriteCmd(uint8_t cmd)
{
    OLED_I2C_Start();
    OLED_I2C_WriteByte(OLED_I2C_ADDR_WRITE);
    OLED_I2C_WriteByte(0x00U);   /* Co=0, D/C#=0 -> 后续为命令 */
    OLED_I2C_WriteByte(cmd);
    OLED_I2C_Stop();
}

/**
 * @brief  向 SSD1306 写数据（显存）
 * @param  data  数据字节
 */
static void OLED_WriteData(uint8_t data)
{
    OLED_I2C_Start();
    OLED_I2C_WriteByte(OLED_I2C_ADDR_WRITE);
    OLED_I2C_WriteByte(0x40U);   /* Co=0, D/C#=1 -> 后续为数据 */
    OLED_I2C_WriteByte(data);
    OLED_I2C_Stop();
}

/* ========== 6x8 ASCII 字库（0x20~0x7E，列优先，每字符6字节） ========== */
static const uint8_t s_font6x8[][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14,0x00}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62,0x00}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50,0x00}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14,0x00}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08,0x00}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08,0x00}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02,0x00}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46,0x00}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31,0x00}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10,0x00}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39,0x00}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30,0x00}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03,0x00}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36,0x00}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E,0x00}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14,0x00}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08,0x00}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06,0x00}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E,0x00}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36,0x00}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22,0x00}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C,0x00}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41,0x00}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01,0x00}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A,0x00}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01,0x00}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41,0x00}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40,0x00}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F,0x00}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E,0x00}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06,0x00}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E,0x00}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46,0x00}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31,0x00}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01,0x00}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F,0x00}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F,0x00}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63,0x00}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07,0x00}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43,0x00}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20,0x00}, /* '\' */
    {0x00,0x41,0x41,0x7F,0x00,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04,0x00}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78,0x00}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38,0x00}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20,0x00}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F,0x00}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18,0x00}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02,0x00}, /* 'f' */
    {0x08,0x14,0x54,0x54,0x3C,0x00}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78,0x00}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78,0x00}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78,0x00}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38,0x00}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08,0x00}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C,0x00}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08,0x00}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20,0x00}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20,0x00}, /* 't' */
    {0x3C,0x40,0x40,0x20,0x7C,0x00}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C,0x00}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44,0x00}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C,0x00}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44,0x00}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00,0x00}, /* '}' */
    {0x10,0x08,0x08,0x10,0x08,0x00}, /* '~' */
};
#define FONT_START_CHAR 0x20U   /* 字库起始字符（空格） */

/**
 * @brief  OLED 初始化（SSD1306 标准初始化序列）
 */
void BSP_OLED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitStruct.Pin   = OLED_SCL_Pin | OLED_SDA_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OLED_SCL_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin | OLED_SDA_Pin, GPIO_PIN_SET);
    BSP_UART1_Printf("[OLED] gpio ok\r\n");
    /* GPIO configured above */

    OLED_WriteCmd(0xAEU);   /* 关闭显示 */
    OLED_WriteCmd(0x20U); OLED_WriteCmd(0x02U);  /* 页寻址模式 */
    OLED_WriteCmd(0xB0U);   /* 页地址 0 */
    OLED_WriteCmd(0xC8U);   /* COM扫描方向：从下到上（适配中景园模块） */
    OLED_WriteCmd(0x00U);   /* 低位列地址 */
    OLED_WriteCmd(0x10U);   /* 高位列地址 */
    OLED_WriteCmd(0x40U);   /* 起始行 0 */
    OLED_WriteCmd(0x81U); OLED_WriteCmd(0x7FU);  /* 对比度 127 */
    OLED_WriteCmd(0xA1U);   /* 段重映射：水平翻转（适配中景园模块） */
    OLED_WriteCmd(0xA6U);   /* 正常显示（非反色） */
    OLED_WriteCmd(0xA8U); OLED_WriteCmd(0x3FU);  /* 复用率 63（1/64占空比） */
    OLED_WriteCmd(0xA4U);   /* 全局显示开（跟随GDDRAM） */
    OLED_WriteCmd(0xD3U); OLED_WriteCmd(0x00U);  /* 显示偏移 0 */
    OLED_WriteCmd(0xD5U); OLED_WriteCmd(0xF0U);  /* 时钟分频：A=0xF(最大), B=0x0 */
    OLED_WriteCmd(0xD9U); OLED_WriteCmd(0x22U);  /* 预充电周期 */
    OLED_WriteCmd(0xDAU); OLED_WriteCmd(0x12U);  /* COM引脚配置：Alternative */
    OLED_WriteCmd(0xDBU); OLED_WriteCmd(0x20U);  /* VCOM检测电平 0.77xVcc */
    OLED_WriteCmd(0x8DU); OLED_WriteCmd(0x14U);  /* 电荷泵使能 */
    BSP_DelayMs(100U);     /* 等待电荷泵稳定 */
    OLED_WriteCmd(0xAFU);   /* 开启显示 */
    BSP_UART1_Printf("[OLED] seq ok\r\n");

    BSP_OLED_Clear();
}

/**
 * @brief  设置光标位置（页寻址模式）
 */
void BSP_OLED_SetCursor(uint8_t page, uint8_t col)
{
    if (page >= OLED_PAGES) { page = OLED_PAGES - 1U; }
    if (col >= OLED_WIDTH)  { col = OLED_WIDTH - 1U; }
    OLED_WriteCmd((uint8_t)(0xB0U + page));
    OLED_WriteCmd((uint8_t)(0x00U + (col & 0x0FU)));       /* 列地址低4位 */
    OLED_WriteCmd((uint8_t)(0x10U + ((col >> 4) & 0x0FU))); /* 列地址高4位 */
}

/**
 * @brief  清屏（逐页逐列写0）
 */
void BSP_OLED_Clear(void)
{
    uint8_t page, col;
    for (page = 0U; page < OLED_PAGES; page++) {
        BSP_OLED_SetCursor(page, 0U);
        for (col = 0U; col < OLED_WIDTH; col++) {
            OLED_WriteData(0x00U);
        }
    }
}

/**
 * @brief  用空格填充整行
 */
void BSP_OLED_ClearLine(uint8_t page)
{
    uint8_t col;
    BSP_OLED_SetCursor(page, 0U);
    for (col = 0U; col < OLED_WIDTH; col++) {
        OLED_WriteData(0x00U);
    }
}

/**
 * @brief  全屏填充（点亮所有像素），用于硬件测试
 * @param  data  填充数据（0xFF=全屏亮，0x00=全屏灭）
 */
void BSP_OLED_Fill(uint8_t data)
{
    uint8_t page, col;
    for (page = 0U; page < OLED_PAGES; page++) {
        BSP_OLED_SetCursor(page, 0U);
        for (col = 0U; col < OLED_WIDTH; col++) {
            OLED_WriteData(data);
        }
    }
}

/**
 * @brief  屏幕反色显示（硬件命令 0xA7/0xA6）
 */
void BSP_OLED_InvertDisplay(uint8_t invert)
{
    OLED_WriteCmd(invert ? 0xA7U : 0xA6U);
}

/**
 * @brief  显示一个 ASCII 字符（6x8）
 */
void BSP_OLED_ShowChar(uint8_t page, uint8_t col, char ch)
{
    uint8_t i;
    const uint8_t *glyph;

    if ((uint8_t)ch < FONT_START_CHAR || (uint8_t)ch > 0x7EU) {
        ch = ' ';   /* 不可打印字符显示为空格 */
    }
    glyph = s_font6x8[(uint8_t)ch - FONT_START_CHAR];

    BSP_OLED_SetCursor(page, col);
    for (i = 0U; i < OLED_FONT6_WIDTH; i++) {
        OLED_WriteData(glyph[i]);
    }
}

/**
 * @brief  显示字符串（6x8，自动排列）
 */
void BSP_OLED_ShowString(uint8_t page, uint8_t col, const char *str)
{
    if (str == 0) { return; }
    while (*str != '\0') {
        if (col >= OLED_WIDTH) { break; }
        BSP_OLED_ShowChar(page, col, *str);
        col += OLED_FONT6_WIDTH;
        str++;
    }
}


/**
 * @brief  显示一个 ASCII 字符（8x16，跨 2 页）
 * @param  page  起始页号 0,2,4,6
 * @param  col   列号 0~120
 * @param  ch    ASCII 字符
 * @note   从已验证的 6x8 字库实时缩放生成：纵向每像素复制2次(8→16)，
 *         横向左右各补1列空白(6→8)。避免手写大字库字模错误导致乱码。
 */
void BSP_OLED_ShowChar8x16(uint8_t page, uint8_t col, char ch)
{
    uint8_t i, j;
    const uint8_t *glyph6;
    uint8_t buf[8];

    if ((uint8_t)ch < FONT_START_CHAR || (uint8_t)ch > 0x7EU) {
        ch = ' ';
    }
    glyph6 = s_font6x8[(uint8_t)ch - FONT_START_CHAR];

    /* 上半页：左补1列空白 + 6列(低4位纵向x2) + 右补1列空白 */
    buf[0] = 0x00U;
    for (i = 0U; i < 6U; i++) {
        uint8_t src = glyph6[i];
        uint8_t dst = 0U;
        for (j = 0U; j < 4U; j++) {
            if (src & (1U << j)) {
                dst |= (3U << (j * 2U));   /* 每像素复制为相邻2位 */
            }
        }
        buf[i + 1U] = dst;
    }
    buf[7] = 0x00U;
    BSP_OLED_SetCursor(page, col);
    for (i = 0U; i < 8U; i++) {
        OLED_WriteData(buf[i]);
    }

    /* 下半页：取高4位纵向x2 */
    buf[0] = 0x00U;
    for (i = 0U; i < 6U; i++) {
        uint8_t src = glyph6[i];
        uint8_t dst = 0U;
        for (j = 0U; j < 4U; j++) {
            if (src & (1U << (j + 4U))) {
                dst |= (3U << (j * 2U));
            }
        }
        buf[i + 1U] = dst;
    }
    buf[7] = 0x00U;
    BSP_OLED_SetCursor(page + 1U, col);
    for (i = 0U; i < 8U; i++) {
        OLED_WriteData(buf[i]);
    }
}

/**
 * @brief  显示字符串（8x16，自动排列）
 * @param  page  起始页号 0,2,4,6
 * @param  col   起始列号
 * @param  str   字符串
 */
void BSP_OLED_ShowString8x16(uint8_t page, uint8_t col, const char *str)
{
    if (str == 0) { return; }
    while (*str != '\0') {
        if (col >= OLED_WIDTH) { break; }
        BSP_OLED_ShowChar8x16(page, col, *str);
        col += OLED_FONT8_WIDTH;
        str++;
    }
}

/**
 * @brief  清除 8x16 字体所在的两行（page 和 page+1）
 * @param  page  起始页号 0,2,4,6
 */
void BSP_OLED_ClearLine8x16(uint8_t page)
{
    uint8_t col;
    BSP_OLED_SetCursor(page, 0U);
    for (col = 0U; col < OLED_WIDTH; col++) {
        OLED_WriteData(0x00U);
    }
    BSP_OLED_SetCursor(page + 1U, 0U);
    for (col = 0U; col < OLED_WIDTH; col++) {
        OLED_WriteData(0x00U);
    }
}

#endif /* BSP_OLED_ENABLE */
