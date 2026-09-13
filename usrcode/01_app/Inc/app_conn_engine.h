/**
 ******************************************************************************
 * @file    app_conn_engine.h
 * @brief   应用层连接引擎 — 把 MQTT 任务连接状态注册为 plat_conn 连接器
 *
 *  作用（对齐 v2.2a app_conn_engine 设计，简化接线）：
 *    - app_mqtt 内部维护 s_mqtt_status（bit0=WiFi  bit1=TCP  bit2=MQTT）
 *    - 本引擎将其注册为 plat_conn 连接器 "mqtt"，对外提供统一状态视图
 *    - 其他模块（OLED/告警/调试）可通过 plat_conn_find("mqtt") 查询
 *      PLAT_CONN_STATE_ONLINE/LINKING/OFFLINE，实现跨模块状态感知
 ******************************************************************************
 */
#ifndef APP_CONN_ENGINE_H
#define APP_CONN_ENGINE_H

#include "module_cfg.h"

#if APP_CONN_ENGINE_ENABLE && PLAT_CONN_ENABLE

#include "plat_conn.h"

/** @brief 获取 MQTT 连接器句柄（供依赖注入/状态查询） */
plat_conn_t *app_conn_engine_mqtt_get(void);

/** @brief 注册 MQTT 连接器到 plat_conn 注册表（APP_Init 中调用） @retval 0=成功 */
uint8_t app_conn_engine_mqtt_register(void);

/** @brief 周期刷新连接器状态（PlatSvcTask 每秒调用） */
void app_conn_engine_process(void);

/** @brief 当前连接状态字符串（ONLINE/LINKING/OFFLINE/FAULT，调试打印用） */
const char *app_conn_engine_state_str(void);

#endif /* APP_CONN_ENGINE_ENABLE && PLAT_CONN_ENABLE */
#endif /* APP_CONN_ENGINE_H */
