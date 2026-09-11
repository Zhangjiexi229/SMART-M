/**
 ******************************************************************************
 * @file    app_ina226.c
 * @brief   INA226电源监测应用任务实现 — 周期采集+过流保护+共享快照
 *
 *  ▍过流保护（毕设核心功能：异常自动断电）：
 *    电流 >= INA226_OVERCURRENT_MA 且继电器吸合 → 断开继电器 + 蜂鸣器报警
 *    受 APP_INA226_PROTECT_ENABLE 开关控制（默认开启，依赖 APP_RELAY_ENABLE）
 ******************************************************************************
 */
#include "module_cfg.h"
#include "app_ina226.h"
#include "cmsis_os.h"

/* ==========================================================================
 *  INA226 采集任务（仅在模块启用时编译）
 * ========================================================================== */
#if APP_INA226_ENABLE && APP_TASKS_ENABLE && BSP_INA226_ENABLE

#include "bsp_ina226.h"
#include "bsp_uart.h"
#if APP_SENSOR_ENABLE
#include "app_sensor.h"
#endif
#if APP_DIAG_ENABLE
#include "app_diag.h"
#endif
#if APP_RELAY_ENABLE && BSP_RELAY_ENABLE
#include "app_relay.h"
#endif
#if BSP_BEEP_ENABLE
#include "bsp_beep.h"
#endif
#if BSP_I2C_SOFT_ENABLE
#include "bsp_i2c_soft.h"
#endif

/* I2C连续失败计数：>=5次触发总线恢复 */
static uint8_t s_i2c_fail_count = 0U;

/* 过流保护总开关：电流越限时自动断开继电器（电机断电保护） */
#ifndef APP_INA226_PROTECT_ENABLE
#define APP_INA226_PROTECT_ENABLE  1U
#endif

/**
 * @brief  INA226电源监测任务
 * @param  argument 未使用
 */
void APP_INA226_Task(void *argument)
{
    (void)argument;

    BSP_INA226_Data_t pwr;
    uint8_t retry;
    uint8_t ok;
    uint32_t sample_cnt = 0U;

    /* 任务启动标识 */
    BSP_UART1_Printf("[INA226] Task started (period=%ums, addr=0x%02X, overcurrent=%dmA)\r\n",
                     (unsigned)INA226_PERIOD_MS, (unsigned)INA226_I2C_ADDR,
                     (int)INA226_OVERCURRENT_MA);

    /* INA226 上电稳定，首次读取前等待 */
    osDelay(500U);

    for (;;) {
        /* 1. 读取电源数据，失败重试 */
        ok = 0U;
        for (retry = 0U; retry < INA226_RETRY_COUNT; retry++) {
            if (BSP_INA226_Read(&pwr) == 0U) {
                ok = 1U;
                break;
            }
            osDelay(100U);
        }

        if (ok != 0U) {
            /* 2. 更新统一快照（供MQTT上报） */
#if APP_SENSOR_ENABLE
            APP_SENSOR_UpdatePower(pwr.bus_mv, pwr.current_ma, pwr.power_mw, 1U);
#endif
            /* 2b. 更新诊断快照（float：A/V/W，供告警/断网补传使用） */
#if APP_DIAG_ENABLE
            APP_DIAG_UpdatePower((float)pwr.current_ma / 1000.0f,
                                 (float)pwr.bus_mv / 1000.0f,
                                 (float)pwr.power_mw / 1000.0f,
                                 1U);
#endif

            /* 3. 过流保护：电流越限 → 断开继电器 + 蜂鸣器报警 */
#if APP_INA226_PROTECT_ENABLE && APP_RELAY_ENABLE && BSP_RELAY_ENABLE
            if ((pwr.current_ma >= INA226_OVERCURRENT_MA) &&
                (APP_Relay_GetState() != 0U)) {
                APP_Relay_Control(0U);   /* 断开继电器，电机断电 */
                BSP_UART1_Printf("[INA226] !! OVERCURRENT %dmA, relay OFF (protection)\r\n",
                                 (int)pwr.current_ma);
#if BSP_BEEP_ENABLE
                BSP_BEEP_Beep(300U);     /* 蜂鸣器报警300ms */
#endif
            }
#endif /* APP_INA226_PROTECT_ENABLE */

            /* 4. 每N次采集打印一次 */
            sample_cnt++;
            if ((sample_cnt % INA226_PRINT_DIV) == 0U) {
                BSP_UART1_Printf("[INA226] Bus=%umV | Shunt=%+duV | I=%+dmA | P=%umW\r\n",
                                 (unsigned)pwr.bus_mv, (int)pwr.shunt_uv,
                                 (int)pwr.current_ma,
                                 (unsigned)pwr.power_mw);
            }
        } else {
#if APP_SENSOR_ENABLE
            /* 读取失败：标记快照无效（保留上一次数值） */
            APP_SENSOR_UpdatePower(0U, 0, 0U, 0U);
#endif
#if APP_DIAG_ENABLE
            APP_DIAG_UpdatePower(0.0f, 0.0f, 0.0f, 0U);
#endif
            BSP_UART1_Printf("[INA226] Read FAILED, retry=%u (check I2C wiring/addr=0x%02X)\r\n",
                             (unsigned)retry, (unsigned)INA226_I2C_ADDR);

            /* I2C连续失败：触发总线恢复 */
#if BSP_I2C_SOFT_ENABLE
            s_i2c_fail_count++;
            if (s_i2c_fail_count >= 5U) {
                BSP_UART1_Printf("[INA226] I2C failed %u times, bus recovering...\r\n",
                                 (unsigned)s_i2c_fail_count);
                BSP_I2C_Soft_Recover();
                s_i2c_fail_count = 0U;
            }
#endif
        }

        /* 5. 任务阻塞，释放CPU */
        osDelay(INA226_PERIOD_MS);
    }
}

#endif /* APP_INA226_ENABLE && APP_TASKS_ENABLE && BSP_INA226_ENABLE */
