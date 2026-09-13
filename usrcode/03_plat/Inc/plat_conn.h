/**
 ******************************************************************************
 * @file    plat_conn.h
 * @brief   平台抽象层（PLAT）— 连接器抽象（Connector Abstraction）
 *
 *  设计目的：
 *    - 把"网络连接"抽象为统一连接器对象（WiFi / 4G / 以太网 / MQTT 等），
 *      上层（svc_conn / svc_telemetry）只面对统一接口，换网络介质不改业务代码
 *    - 连接器 ops 由引擎实现注入（app 层接线），本层不依赖任何具体网络栈
 *
 *  状态位约定（status 返回值）：
 *    bit0 = 链路层已建立（如 WiFi 关联/PPP 拨号）
 *    bit1 = 传输层已建立（如 TCP 连接）
 *    bit2 = 应用层已就绪（如 MQTT CONNECT 完成）
 ******************************************************************************
 */
#ifndef PLAT_CONN_H
#define PLAT_CONN_H

#include <stdint.h>
#include "plat_obj.h"   /* 复用对象模型命名规范 */

/* ========== 连接器统一状态 ========== */
typedef enum {
    PLAT_CONN_STATE_OFFLINE = 0,   /* 完全离线 */
    PLAT_CONN_STATE_LINKING,       /* 正在建链（链路/传输/应用逐层进行中） */
    PLAT_CONN_STATE_ONLINE,        /* 应用层就绪，可收发 */
    PLAT_CONN_STATE_FAULT          /* 故障（重连超限等，由上层置位） */
} plat_conn_state_t;

/* ========== 连接器统一操作集 ========== */
typedef struct {
    uint8_t (*open)(void);                          /* 建立连接（可空：由底层任务管理） */
    uint8_t (*send)(const uint8_t *data, uint16_t len); /* 发送数据 */
    uint8_t (*recv)(uint8_t *data, uint16_t max_len, uint16_t *got); /* 接收数据 */
    uint8_t (*close)(void);                         /* 断开连接 */
    uint8_t (*status)(void);                        /* 状态位：bit0=链路 bit1=传输 bit2=应用 */
} plat_conn_ops_t;

/* ========== 连接器对象 ========== */
typedef struct plat_conn {
    const char             *name;    /* 连接器名，如 "wifi" / "mqtt" / "eth" */
    uint8_t                 id;      /* 连接器ID */
    const void             *cfg;     /* 静态配置（可空） */
    void                   *ctx;     /* 运行上下文（可空） */
    plat_conn_state_t       state;   /* 当前状态（plat_conn_refresh_state 维护） */
    const plat_conn_ops_t  *ops;     /* 统一操作集（引擎注入） */
    struct plat_conn       *next;    /* 注册表链表指针 */
} plat_conn_t;

/* ========== 连接器注册表 ========== */

/** @brief 注册连接器（重名拒绝） @retval 0=成功 */
uint8_t plat_conn_register(plat_conn_t *c);

/** @brief 注销连接器 @retval 0=成功 */
uint8_t plat_conn_unregister(plat_conn_t *c);

/** @brief 按名查找连接器 */
plat_conn_t *plat_conn_find(const char *name);

/** @brief 已注册连接器数量 */
uint16_t plat_conn_count(void);

/** @brief 读取状态位（转发 ops.status，ops 为空返回0） */
uint8_t plat_conn_status(plat_conn_t *c);

/**
 * @brief  依据状态位刷新统一状态机
 * @note   bit2→ONLINE；bit0/bit1→LINKING；全0→OFFLINE；FAULT 由上层显式设置
 * @retval 刷新后的 plat_conn_state_t
 */
plat_conn_state_t plat_conn_refresh_state(plat_conn_t *c);

#endif /* PLAT_CONN_H */
