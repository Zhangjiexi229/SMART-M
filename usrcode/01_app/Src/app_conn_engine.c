/**
 ******************************************************************************
 * @file    app_conn_engine.c
 * @brief   应用层连接引擎 — MQTT 连接器注册与状态刷新（plat_conn 接线）
 *
 *  状态位约定（与 plat_conn.h 一致）：
 *    bit0 = 链路层已建立（WiFi 关联）
 *    bit1 = 传输层已建立（TCP 连接）
 *    bit2 = 应用层已就绪（MQTT CONNECT 完成）
 *
 *  app_mqtt 内部维护 s_mqtt_status 即按此约定置位，
 *  本引擎仅转发该状态到 plat_conn 连接器，不改变 app_mqtt 逻辑。
 ******************************************************************************
 */
#include "module_cfg.h"

#if APP_CONN_ENGINE_ENABLE && PLAT_CONN_ENABLE && APP_MQTT_ENABLE

#include "app_conn_engine.h"
#include "app_mqtt.h"

/* ========== 连接器 ops：status 转发 app_mqtt 状态位 ========== */

static uint8_t mqtt_conn_status(void)
{
    return APP_MQTT_GetStatus();
}

static const plat_conn_ops_t s_mqtt_conn_ops = {
    .open   = NULL,   /* 建连由 app_mqtt 任务自身管理 */
    .send   = NULL,
    .recv   = NULL,
    .close  = NULL,
    .status = mqtt_conn_status,
};

/* ========== MQTT 连接器对象 ========== */

static plat_conn_t s_mqtt_conn = {
    .name  = "mqtt",
    .id    = 0U,
    .cfg   = NULL,
    .ctx   = NULL,
    .state = PLAT_CONN_STATE_OFFLINE,
    .ops   = &s_mqtt_conn_ops,
    .next  = NULL,
};

/* ========== 引擎接口 ========== */

plat_conn_t *app_conn_engine_mqtt_get(void)
{
    return &s_mqtt_conn;
}

uint8_t app_conn_engine_mqtt_register(void)
{
    return plat_conn_register(&s_mqtt_conn);
}

void app_conn_engine_process(void)
{
    (void)plat_conn_refresh_state(&s_mqtt_conn);
}

const char *app_conn_engine_state_str(void)
{
    switch (plat_conn_refresh_state(&s_mqtt_conn)) {
    case PLAT_CONN_STATE_ONLINE:  return "ONLINE";
    case PLAT_CONN_STATE_LINKING: return "LINKING";
    case PLAT_CONN_STATE_FAULT:   return "FAULT";
    default:                      return "OFFLINE";
    }
}

#endif /* APP_CONN_ENGINE_ENABLE && PLAT_CONN_ENABLE && APP_MQTT_ENABLE */
