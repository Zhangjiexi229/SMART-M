/**
 ******************************************************************************
 * @file    plat_obj.h
 * @brief   平台抽象层（PLAT）— 对象模型（Object Model）
 *
 *  对象模型四元组：cfg(配置) / ctx(上下文) / data(数据) / ops(操作集)
 *  统一生命周期原语：register → init → start → process → stop → deinit
 *  说明：
 *    - 本层命名 PLAT（Platform Abstraction Layer），避免与 STM32 HAL 库冲突
 *    - 设备(plat_dev_t)与服务(plat_svc_t)统一建模，由管理器统一驱动
 *    - APP/Service 通过句柄+ops 访问设备，不直接调用 BSP 驱动
 ******************************************************************************
 */
#ifndef PLAT_OBJ_H
#define PLAT_OBJ_H

#include <stdint.h>
#include <stddef.h>   /* NULL */

/* ========== 生命周期状态 ========== */
typedef enum {
    PLAT_STATE_NONE       = 0,  /* 未注册/已注销 */
    PLAT_STATE_REGISTERED,      /* 已注册 */
    PLAT_STATE_INITED,          /* 已初始化 */
    PLAT_STATE_STARTED,         /* 已启动（周期 process 有效） */
    PLAT_STATE_STOPPED,         /* 已停止 */
    PLAT_STATE_DEINITED         /* 已反初始化 */
} plat_state_t;

/* ========== 统一生命周期操作集（四元组中的 ops） ========== */
typedef struct {
    uint8_t (*init)(void);      /* 初始化：加载配置、准备资源；返回0=成功 */
    uint8_t (*start)(void);     /* 启动：开始周期运行；可空 */
    uint8_t (*process)(void);   /* 周期处理：采集/计算/输出；可空 */
    uint8_t (*stop)(void);      /* 停止；可空 */
    uint8_t (*deinit)(void);    /* 反初始化：释放资源；可空 */
} plat_ops_t;

/* ========== 设备对象（Device） ========== */
typedef struct plat_dev {
    const char       *name;     /* 唯一设备名，如 "eeprom" */
    uint8_t           id;       /* 设备ID（板级表内唯一） */
    const void       *cfg;      /* 静态配置：引脚/地址/参数（只读） */
    void             *ctx;      /* 运行上下文：句柄/缓冲/内部状态 */
    void             *data;     /* 业务数据：采集结果/状态（供上层读取） */
    const plat_ops_t *ops;      /* 统一生命周期操作集（项可空） */
    plat_state_t      state;    /* 当前生命周期状态（管理器维护） */
    struct plat_dev  *next;     /* 管理器链表指针（内部使用） */
} plat_dev_t;

/* ========== 服务对象（Service） ========== */
typedef struct plat_svc {
    const char       *name;     /* 服务名，如 "storage" */
    uint8_t           id;       /* 服务ID */
    void             *ctx;      /* 服务上下文 */
    const plat_ops_t *ops;      /* 服务生命周期操作集（项可空） */
    plat_state_t      state;    /* 当前生命周期状态（管理器维护） */
    plat_dev_t      **deps;     /* 依赖设备句柄数组（依赖注入，可空） */
    uint8_t           dep_count;/* 依赖设备数量 */
    struct plat_svc  *next;     /* 管理器链表指针（内部使用） */
} plat_svc_t;

/* ========== 便捷查询 ========== */

/** @brief 设备是否可用（已初始化及以上） */
static inline uint8_t plat_dev_is_ready(const plat_dev_t *dev)
{
    return ((dev != NULL) && (dev->state >= PLAT_STATE_INITED)) ? 1U : 0U;
}

/** @brief 服务是否运行中 */
static inline uint8_t plat_svc_is_running(const plat_svc_t *svc)
{
    return ((svc != NULL) && (svc->state == PLAT_STATE_STARTED)) ? 1U : 0U;
}

#endif /* PLAT_OBJ_H */
