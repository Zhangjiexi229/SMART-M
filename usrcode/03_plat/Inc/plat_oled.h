/**
 ******************************************************************************
 * @file    plat_oled.h
 * @brief   平台抽象层（PLAT）— OLED 设备对象化封装
 *
 *  设计说明：
 *    - 将 BSP 驱动（bsp_oled.c）包装为统一设备对象 g_oled_dev，
 *      注册进 device manager，由 board manager 统一管理生命周期
 *    - 提供便捷显示接口（转发 BSP），供 Service/APP 层使用
 ******************************************************************************
 */
#ifndef PLAT_OLED_H
#define PLAT_OLED_H

#include <stdint.h>
#include "plat_obj.h"   /* 对象模型：plat_dev_t */

/* ========== 便捷显示接口（转发 BSP，Service/APP 层调用） ========== */

/** @brief 清屏 */
void plat_oled_clear(void);

/** @brief 设置光标（页寻址模式） */
void plat_oled_set_cursor(uint8_t page, uint8_t col);

/** @brief 显示字符串（6x8 小字体） */
void plat_oled_show_string(uint8_t page, uint8_t col, const char *str);

/** @brief 显示字符串（8x16 大字体） */
void plat_oled_show_string8x16(uint8_t page, uint8_t col, const char *str);

/** @brief 清除某页内容（6x8） */
void plat_oled_clear_line(uint8_t page);

/** @brief 全屏填充（0xFF=亮 0x00=灭，硬件测试用） */
void plat_oled_fill(uint8_t data);

/* ========== 对象化设备封装（注册到 device manager） ========== */

/** @brief 获取 OLED 设备对象句柄（依赖注入用） */
plat_dev_t *plat_oled_get_dev(void);

/** @brief 将 OLED 设备注册到设备管理器 */
uint8_t plat_oled_register(void);

#endif /* PLAT_OLED_H */
