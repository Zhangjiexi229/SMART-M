/**
 ******************************************************************************
 * @file    plat_brd.h
 * @brief   平台抽象层（PLAT）— 板级资源建模 + 板级管理器（board manager）
 *
 *  板级资源建模：
 *    - pinmap   : 设备→引脚映射（主引脚 + 辅助引脚）
 *    - busmap   : 总线名→类型/实例映射
 *    - powermap : 电源域定义（预留）
 *    - irqmap   : 中断定义（预留）
 *  板级设备表 plat_brd_t：声明板上对象化设备清单
 *  board manager 按表批量注册设备到 device manager，供依赖注入
 ******************************************************************************
 */
#ifndef PLAT_BRD_H
#define PLAT_BRD_H

#include "plat_obj.h"
#include "plat_mgr.h"
#include "stm32f4xx_hal.h"

#define PLAT_BRD_MAX_DEV    16U   /* 单板最大对象化设备数 */

/* ========== 引脚映射表（pinmap） ========== */
typedef struct {
    const char   *dev_name;   /* 设备名（与 plat_dev_t.name 对应） */
    const char   *bus_name;   /* 所属总线名，如 "i2c_sw0" / "spi1" / "gpio" */
    GPIO_TypeDef *port;       /* 主引脚端口（可空） */
    uint16_t      pin;        /* 主引脚号 */
    GPIO_TypeDef *aux_port;   /* 辅助引脚端口（如 SCL/CS，可空） */
    uint16_t      aux_pin;    /* 辅助引脚号 */
} plat_pinmap_t;

/* ========== 总线映射表（busmap） ========== */
typedef struct {
    const char *bus_name;     /* 总线名 */
    const char *bus_type;     /* 总线类型：i2c / spi / uart / adc / gpio */
    uint8_t     bus_idx;      /* 实例序号 0/1/2... */
} plat_busmap_t;

/* ========== 电源域映射表（powermap，预留） ========== */
typedef struct {
    const char *name;         /* 电源域名 */
    uint8_t     default_on;   /* 默认上电：1=on 0=off */
} plat_powermap_t;

/* ========== 中断映射表（irqmap，预留） ========== */
typedef struct {
    const char *name;         /* 中断名 */
    IRQn_Type   irq;          /* IRQn */
    uint8_t     priority;     /* 抢占优先级 */
} plat_irqmap_t;

/* ========== 板级设备表（board devices，注入源） ========== */
typedef struct {
    uint16_t      dev_count;                  /* 板载设备数量 */
    plat_dev_t   *devs[PLAT_BRD_MAX_DEV];     /* 设备对象指针数组 */
} plat_brd_t;

/* 本板板级资源表（定义于 plat_brd.c） */
extern const plat_pinmap_t g_plat_pinmap[];
extern const uint16_t g_plat_pinmap_count;
extern const plat_busmap_t g_plat_busmap[];
extern const uint16_t g_plat_busmap_count;
extern plat_brd_t g_plat_brd;

/* ========== 板级管理器（board manager） ========== */
uint8_t plat_brdmgr_init(void);                        /* 注册板级表全部设备到 devmgr */
plat_dev_t *plat_brdmgr_get(const char *name);         /* 按名取设备（依赖注入入口） */
const plat_pinmap_t *plat_brdmgr_find_pinmap(const char *dev_name); /* 查引脚映射 */

#endif /* PLAT_BRD_H */
