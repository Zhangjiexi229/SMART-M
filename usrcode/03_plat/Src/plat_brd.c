/**
 ******************************************************************************
 * @file    plat_brd.c
 * @brief   平台抽象层（PLAT）— SMART-M 板级资源表（board devices）
 *
 *  内容：
 *    - pinmap：设备→引脚映射（与 main.h / bsp 驱动引脚宏保持一致）
 *    - busmap：总线定义
 *    - g_plat_brd：板载对象化设备清单（plat_brd_t）
 *
 *  说明：
 *    - 新增对象化设备时：在 plat_brd_t.devs[] 追加设备对象指针，并同步 pinmap
 *    - 设备对象定义于各 plat_*_dev 封装（如 plat_oled.c 的 g_oled_dev）
 *    - 本板无 AT24C02/W25Q128/DHT11，仅对象化 OLED；网络连接器（mqtt）
 *      走 plat_conn 注册表（见 app_conn_engine.c），不在此表
 ******************************************************************************
 */
#include "module_cfg.h"
#if PLAT_BRDMGR_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE && PLAT_OLED_ENABLE

#include "plat_brd.h"
#include "main.h"

/* ========== 引脚映射表（pinmap） ========== */
const plat_pinmap_t g_plat_pinmap[] = {
    /* 设备名,          总线名,      主引脚端口,    主引脚,          辅助引脚端口,  辅助引脚 */
    { "oled",          "i2c_sw1",   GPIOD,       GPIO_PIN_6,     GPIOD,       GPIO_PIN_7 },  /* PD6=SCL, PD7=SDA（SSD1306） */
    { "relay",         "gpio",      GPIOA,       GPIO_PIN_4,     NULL,        0U },          /* PA4 继电器 */
    { "beep",          "gpio",      GPIOA,       GPIO_PIN_5,     NULL,        0U },          /* PA5 蜂鸣器 */
    { "key_matrix",    "gpio",      GPIOE,       GPIO_PIN_2,     GPIOE,       GPIO_PIN_12 }, /* 矩阵键盘 PE2..PE12 */
};
const uint16_t g_plat_pinmap_count = (uint16_t)(sizeof(g_plat_pinmap) / sizeof(g_plat_pinmap[0]));

/* ========== 总线映射表（busmap） ========== */
const plat_busmap_t g_plat_busmap[] = {
    { "i2c_sw1", "i2c", 1U },   /* OLED 软件I2C（PD6/PD7） */
    { "gpio",    "gpio", 0U },  /* 普通GPIO设备（继电器/蜂鸣器/矩阵键盘等） */
};
const uint16_t g_plat_busmap_count = (uint16_t)(sizeof(g_plat_busmap) / sizeof(g_plat_busmap[0]));

/* ========== 板级设备表（board devices） ========== */
extern plat_dev_t g_oled_dev;     /* OLED 设备对象（定义于 plat_oled.c） */

plat_brd_t g_plat_brd = {
    .dev_count = 1U,
    .devs = {
        &g_oled_dev,     /* 显示：SSD1306 */
    },
};

#endif /* PLAT_BRDMGR_ENABLE && PLAT_DEVMGR_ENABLE && PLAT_OBJ_ENABLE && PLAT_OLED_ENABLE */
