/**
 ******************************************************************************
 * @file    bsp_oled.h
 * @brief   OLED驱动头文件 — 0.96英寸 SSD1306，PD6/PD7 软件I2C
 *
 *  硬件连接：
 *    OLED SCL -> PD6（软件I2C时钟线，推挽输出）
 *    OLED SDA -> PD7（软件I2C数据线，推挽输出）
 *    （中景园 0.96" 128x64，I2C 地址 0x3C，模块自带 I2C 上拉电阻）
 *
 *  字库：
 *    - 6x8  小字体：每字符 6x8 像素，占 1 页，每行 21 字符，共 8 行
 *    - 8x16 大字体：每字符 8x16 像素，占 2 页，每行 16 字符，共 4 行
 *
 *  设计要点：
 *    - GPIO 由 CubeMX 的 MX_GPIO_Init() 配置为推挽输出、高速、初始高电平
 *    - SDA 用推挽输出 + 不检测 ACK（SSD1306 为唯一从机，写操作稳定可靠）
 *    - 微秒延时走 bsp_delay.h（TIM2 自由计数器），与 DHT11 共享安全
 ******************************************************************************
 */
#ifndef BSP_OLED_H
#define BSP_OLED_H

#include <stdint.h>

/* ========== I2C 地址 ========== */
#define OLED_I2C_ADDR       0x3CU   /* 中景园 SSD1306 模块地址（SA0=0） */
#define OLED_I2C_ADDR_WRITE 0x78U   /* 写地址 = (0x3C<<1) = 0x78 */

/* ========== 显示参数 ========== */
#define OLED_WIDTH          128U
#define OLED_HEIGHT         64U
#define OLED_PAGES          8U      /* 64/8 = 8 页 */

/* 6x8 小字体 */
#define OLED_FONT6_WIDTH    6U
#define OLED_FONT6_HEIGHT   8U
#define OLED_MAX_CHARS_6    (OLED_WIDTH / OLED_FONT6_WIDTH)  /* 21 */

/* 8x16 大字体 */
#define OLED_FONT8_WIDTH    8U
#define OLED_FONT8_HEIGHT   16U
#define OLED_MAX_CHARS_8    (OLED_WIDTH / OLED_FONT8_WIDTH)  /* 16 */

/**
 * @brief  OLED 初始化
 * @note   GPIO 已由 MX_GPIO_Init() 配置；本函数执行 SSD1306 初始化序列并清屏
 */
void BSP_OLED_Init(void);

/**
 * @brief  清屏（全屏写0）
 */
void BSP_OLED_Clear(void);

/**
 * @brief  设置光标位置（页寻址模式）
 * @param  page  页号 0~7（对应纵向8像素行）
 * @param  col   列号 0~127
 */
void BSP_OLED_SetCursor(uint8_t page, uint8_t col);

/* ========== 6x8 小字体 ========== */

/**
 * @brief  在指定位置显示一个 ASCII 字符（6x8）
 * @param  page  页号 0~7
 * @param  col   列号 0~127
 * @param  ch    ASCII 字符（可显示字符 0x20~0x7E）
 */
void BSP_OLED_ShowChar(uint8_t page, uint8_t col, char ch);

/**
 * @brief  在指定位置显示字符串（6x8，自动从左到右排列）
 * @param  page  页号 0~7
 * @param  col   起始列号 0~127
 * @param  str   以 '\0' 结尾的字符串
 */
void BSP_OLED_ShowString(uint8_t page, uint8_t col, const char *str);

/**
 * @brief  用空格填充整行（清除某一页的内容）
 * @param  page  页号 0~7
 */
void BSP_OLED_ClearLine(uint8_t page);

/* ========== 8x16 大字体 ========== */

/**
 * @brief  在指定位置显示一个 ASCII 字符（8x16，跨 2 页）
 * @param  page  起始页号 0,2,4,6（字符占 page 和 page+1 两页）
 * @param  col   列号 0~120（8像素对齐，实际按 8 像素步进）
 * @param  ch    ASCII 字符（可显示字符 0x20~0x7E）
 */
void BSP_OLED_ShowChar8x16(uint8_t page, uint8_t col, char ch);

/**
 * @brief  在指定位置显示字符串（8x16，自动从左到右排列）
 * @param  page  起始页号 0,2,4,6
 * @param  col   起始列号 0~120
 * @param  str   以 '\0' 结尾的字符串
 */
void BSP_OLED_ShowString8x16(uint8_t page, uint8_t col, const char *str);

/**
 * @brief  清除 8x16 字体所在的两行（page 和 page+1）
 * @param  page  起始页号 0,2,4,6
 */
void BSP_OLED_ClearLine8x16(uint8_t page);

/* ========== 全屏操作 ========== */

/**
 * @brief  全屏填充（点亮所有像素），用于硬件测试
 * @param  data  填充数据（0xFF=全屏亮，0x00=全屏灭）
 */
void BSP_OLED_Fill(uint8_t data);

/**
 * @brief  屏幕反色显示（硬件命令，不占用显存）
 * @param  invert  1=反色(0xA7)，0=正常(0xA6)
 * @note   用于告警时整屏反白闪烁；反复调用可产生闪烁效果
 */
void BSP_OLED_InvertDisplay(uint8_t invert);

#endif /* BSP_OLED_H */
