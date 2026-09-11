/**
 ******************************************************************************
 * @file    app_sht30.c
 * @brief   SHT30温湿度应用任务实现 — 周期采集+串口打印+共享快照
 *
 *  任务流程：
 *    1. BSP_SHT30_Read() 读取温湿度（失败重试3次，重试间隔200ms）
 *    2. 更新统一快照（app_sensor，供MQTT上报）
 *    3. BSP_UART1_Printf() 打印温湿度
 *    4. osDelay(2000ms) 阻塞释放CPU
 ******************************************************************************
 */
#include "module_cfg.h"
#include "app_sht30.h"
#include "cmsis_os.h"

/* ==========================================================================
 *  SHT30 采集任务（仅在模块启用时编译）
 * ========================================================================== */
#if APP_SHT30_ENABLE && APP_TASKS_ENABLE && BSP_SHT30_ENABLE

#include "bsp_sht30.h"
#include "bsp_uart.h"
#if APP_SENSOR_ENABLE
#include "app_sensor.h"
#endif
#if APP_DIAG_ENABLE
#include "app_diag.h"
#endif
#if BSP_I2C_SOFT_ENABLE
#include "bsp_i2c_soft.h"
#endif

/* I2C连续失败计数：>=5次触发总线恢复（防SDA被拉低死锁） */
static uint8_t s_i2c_fail_count = 0U;

/**
 * @brief  SHT30温湿度采集任务
 * @param  argument 未使用
 */
void APP_SHT30_Task(void *argument)
{
    (void)argument;

    BSP_SHT30_Data_t sht_data;
    uint8_t retry;
    uint8_t ok = 0U;

    /* 最简打印：一行短字符串，看能不能完整打出来 */
    BSP_UART1_Printf("[SHT30] boot\r\n");
    BSP_UART1_Printf("[SHT30] boot2\r\n");

    /* SHT30 上电稳定，首次读取前等待 */
    osDelay(500U);
    BSP_UART1_Printf("[SHT30] >> after osDelay, first read\r\n");

    for (;;) {
        /* 1. 读取SHT30，失败重试 */
        ok = 0U;
        for (retry = 0U; retry < SHT30_RETRY_COUNT; retry++) {
            if (BSP_SHT30_Read(&sht_data) == 0U) {
                ok = 1U;
                break;
            }
            osDelay(200U);  /* 重试间隔200ms */
        }

        if (ok != 0U) {
            /* 2. 更新统一快照（供MQTT上报） */
#if APP_SENSOR_ENABLE
            APP_SENSOR_UpdateSHT30(sht_data.temperature_int, sht_data.temperature_dec,
                                   sht_data.humidity_int,    sht_data.humidity_dec,
                                   1U);
#endif
            /* 3. 更新诊断快照（float，供告警/诊断/断网补传使用） */
#if APP_DIAG_ENABLE
            {
                float temp_c = (float)sht_data.temperature_int +
                               (float)sht_data.temperature_dec * 0.1f;
                float hum_rh = (float)sht_data.humidity_int +
                               (float)sht_data.humidity_dec * 0.1f;
                APP_DIAG_UpdateTempHum(temp_c, hum_rh, 1U);
            }
#endif

            /* 4. 串口打印温湿度（暂时屏蔽，只留MQTT） */
            // BSP_UART1_Printf("[SHT30] Temp=%d.%uC | Hum=%u.%u%%\r\n",
            //                  (int)sht_data.temperature_int, sht_data.temperature_dec,
            //                  sht_data.humidity_int, sht_data.humidity_dec);
        } else {
#if APP_SENSOR_ENABLE
            /* 读取失败：标记快照无效（保留上一次数值供云端参考） */
            APP_SENSOR_UpdateSHT30(sht_data.temperature_int, sht_data.temperature_dec,
                                   sht_data.humidity_int,    sht_data.humidity_dec,
                                   0U);
#endif
#if APP_DIAG_ENABLE
            APP_DIAG_UpdateTempHum(0.0f, 0.0f, 0U);
#endif
            BSP_UART1_Printf("[SHT30] Read FAILED, retry=%u (check I2C wiring/addr=0x%02X)\r\n",
                             (unsigned)retry, (unsigned)SHT30_I2C_ADDR);

            /* I2C连续失败：触发总线恢复 */
#if BSP_I2C_SOFT_ENABLE
            s_i2c_fail_count++;
            if (s_i2c_fail_count >= 5U) {
                BSP_UART1_Printf("[SHT30] I2C failed %u times, bus recovering...\r\n",
                                 (unsigned)s_i2c_fail_count);
                BSP_I2C_Soft_Recover();
                s_i2c_fail_count = 0U;
            }
#endif
        }

        /* 4. 任务阻塞，释放CPU */
        osDelay(SHT30_PERIOD_MS);
    }
}

#endif /* APP_SHT30_ENABLE && APP_TASKS_ENABLE && BSP_SHT30_ENABLE */
