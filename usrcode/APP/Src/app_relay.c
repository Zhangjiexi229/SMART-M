/**
 ******************************************************************************
 * @file    app_relay.c
 * @brief   继电器应用层实现 — 统一控制入口 + 状态快照
 *
 *  所有继电器操作（MQTT下行命令、INA226过流保护、本地面板按键）
 *  都经由 APP_Relay_Control() 完成，保证状态唯一且快照同步。
 ******************************************************************************
 */
#include "module_cfg.h"
#include "app_relay.h"
#include "cmsis_os.h"

/* ==========================================================================
 *  继电器应用（BSP 驱动存在时编译）
 * ========================================================================== */
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE

#include "bsp_relay.h"
#if APP_SENSOR_ENABLE
#include "app_sensor.h"
#endif

void APP_Relay_Control(uint8_t on)
{
    uint8_t cur;

    cur = BSP_RELAY_GetState();
    if (on != 0U) {
        if (cur == 0U) {
            BSP_RELAY_On();
        }
    } else {
        if (cur != 0U) {
            BSP_RELAY_Off();
        }
    }

#if APP_SENSOR_ENABLE
    /* 同步更新统一快照（MQTT上报直接读快照） */
    APP_SENSOR_UpdateRelay((on != 0U) ? 1U : 0U, 1U);
#endif
}

uint8_t APP_Relay_GetState(void)
{
    return BSP_RELAY_GetState();
}

#endif /* APP_RELAY_ENABLE && BSP_RELAY_ENABLE */
