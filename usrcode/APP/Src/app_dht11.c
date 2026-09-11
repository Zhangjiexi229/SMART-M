/**
 ******************************************************************************
 * @file    app_dht11.c
 * @brief   DHT11温湿度传感器应用任务实现 — 周期采集+串口打印+OLED显示+LED联动
 *
 *  任务流程：
 *    1. BSP_DHT11_Read() 读取温湿度（失败重试3次）
 *    2. 温度判定：>=TEMP_THRESHOLD → LED1亮/LED2亮；< → LED1灭/LED2亮
 *    3. 湿度判定：>=HUMIDITY_TH_HIGH → LED3亮/LED4亮；< → LED3灭/LED4亮
 *    4. APP_OLED_UpdateTempHum() 温湿度显示到 OLED（互斥锁保护，数值变化才刷新）
 *    5. BSP_UART1_Printf() 打印温湿度数据和LED状态
 *    6. 更新共享快照 s_snapshot（供WiFi任务读取上报）
 *    7. osDelay(2000ms) 阻塞，释放CPU
 *
 *  LED控制表：
 *    条件              LED1(PG14)  LED2(PG13)  LED3(PG6)  LED4(PG11)
 *    温度>=28℃         亮           亮           -          -
 *    温度<28℃          灭           亮           -          -
 *    湿度>=60%         -            -            亮         亮
 *    湿度<60%          -            -            灭         亮
 *
 *  LED控制受 BSP_LED_ENABLE && APP_DHT11_LED_ENABLE 条件编译保护。
 ******************************************************************************
 */
#include "module_cfg.h"
#include "app_dht11.h"
#include "cmsis_os.h"

/* ==========================================================================
 *  共享快照（始终可用，不依赖 APP_DHT11_ENABLE）
 *  DHT11任务写，WiFi任务读，临界区保护
 * ========================================================================== */
static DHT11_Snapshot_t s_snapshot = {0};

void APP_DHT11_GetSnapshot(DHT11_Snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL();
    *out = s_snapshot;
    taskEXIT_CRITICAL();
}

/* ==========================================================================
 *  DHT11 采集任务（仅在模块启用时编译）
 * ========================================================================== */
#if APP_DHT11_ENABLE && APP_TASKS_ENABLE && BSP_DHT11_ENABLE

#include "bsp_dht11.h"
#if BSP_LED_ENABLE
#include "bsp_led.h"
#endif

#include "bsp_uart.h"
#if APP_SENSOR_ENABLE
#include "app_sensor.h"
#endif
#if APP_OLED_ENABLE && BSP_OLED_ENABLE
#include "app_oled.h"
#endif

/**
 * @brief  更新共享快照（内部使用，临界区保护）
 */
static void APP_DHT11_UpdateSnapshot(uint8_t t_int, uint8_t t_dec,
                                     uint8_t h_int, uint8_t h_dec, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.temperature_int = t_int;
    s_snapshot.temperature_dec = t_dec;
    s_snapshot.humidity_int    = h_int;
    s_snapshot.humidity_dec    = h_dec;
    s_snapshot.valid           = valid;
    taskEXIT_CRITICAL();
}

/**
 * @brief  根据温度控制LED1/LED2
 * @param  temp_int  温度整数部分(℃)
 */
static void APP_DHT11_ControlTempLED(uint8_t temp_int)
{
#if BSP_LED_ENABLE && APP_DHT11_LED_ENABLE
    if (temp_int >= TEMP_THRESHOLD) {
        /* 高温：LED1亮, LED2亮 */
        BSP_LED_On(BSP_LED1);
        BSP_LED_On(BSP_LED2);
    } else {
        /* 正常：LED1灭, LED2亮 */
        BSP_LED_Off(BSP_LED1);
        BSP_LED_On(BSP_LED2);
    }
#else
    (void)temp_int;
#endif
}

/**
 * @brief  根据湿度控制LED3/LED4
 * @param  hum_int  湿度整数部分(%)
 */
static void APP_DHT11_ControlHumLED(uint8_t hum_int)
{
#if BSP_LED_ENABLE && APP_DHT11_LED_ENABLE
    if (hum_int >= HUMIDITY_TH_HIGH) {
        /* 高湿：LED3亮, LED4亮 */
        BSP_LED_On(BSP_LED3);
        BSP_LED_On(BSP_LED4);
    } else {
        /* 正常（含28%~60%区间）：LED3灭, LED4亮 */
        BSP_LED_Off(BSP_LED3);
        BSP_LED_On(BSP_LED4);
    }
#else
    (void)hum_int;
#endif
}

/**
 * @brief  DHT11温湿度采集任务
 * @param  argument 未使用
 */
void APP_DHT11_Task(void *argument)
{
    (void)argument;

    BSP_DHT11_Data_t dht_data;
    BSP_DHT11_Status_t status;
    uint8_t retry;
    const char *temp_state;
    const char *hum_state;

    /* 任务启动标识 */
    BSP_UART1_Printf("[DHT11] Task started (period=%ums, retry=%u)\r\n",
                     (unsigned)DHT11_PERIOD_MS, (unsigned)DHT11_RETRY_COUNT);

    /* DHT11上电后需约1秒稳定期，首次读取前等待，避免第一次读取超时 */
    osDelay(1000U);

    /* 丢弃首次读取：DHT11 上电/复位后第一次采样结果经常无效，仅作预热 */
    {
        BSP_DHT11_Data_t warmup;
        (void)BSP_DHT11_Read(&warmup);
    }
    osDelay(100U);  /* 保证两次读取间隔，避免预热后总线时序异常 */

    BSP_DHT11_SelfTest();   /* 硬件自检：打印TIM2/GPIO/总线应答诊断（定位后可删除） */

    for (;;) {
        /* 1. 读取DHT11，失败重试 */
        status = BSP_DHT11_ERR_TIMEOUT;
        for (retry = 0U; retry < DHT11_RETRY_COUNT; retry++) {
            status = BSP_DHT11_Read(&dht_data);
            if (status == BSP_DHT11_OK) {
                break;
            }
            osDelay(100U);  /* 重试间隔100ms */
        }

        if (status == BSP_DHT11_OK) {
            /* 2. 温度判定 + LED控制 */
            APP_DHT11_ControlTempLED(dht_data.temperature_int);
            temp_state = (dht_data.temperature_int >= TEMP_THRESHOLD) ? "HIGH" : "OK  ";

            /* 3. 湿度判定 + LED控制 */
            APP_DHT11_ControlHumLED(dht_data.humidity_int);
            hum_state = (dht_data.humidity_int >= HUMIDITY_TH_HIGH) ? "HIGH" : "OK  ";

#if APP_SENSOR_ENABLE
            /* 发布到共享传感器快照（供WiFi云端上报消费） */
            APP_SENSOR_UpdateTempHum(dht_data.temperature_int, dht_data.temperature_dec,
                                     dht_data.humidity_int,    dht_data.humidity_dec,
                                     1U);
#endif

#if APP_OLED_ENABLE && BSP_OLED_ENABLE
            /* 4. 温湿度显示到 OLED（内部互斥锁，防与光敏任务并发写屏） */
            APP_OLED_UpdateTempHum(dht_data.temperature_int, dht_data.temperature_dec,
                            dht_data.humidity_int,    dht_data.humidity_dec);
#endif

            /* 5. 更新共享快照（供WiFi任务读取上报） */
            APP_DHT11_UpdateSnapshot(dht_data.temperature_int, dht_data.temperature_dec,
                                     dht_data.humidity_int,    dht_data.humidity_dec, 1U);

            /* 6. 串口打印温湿度和LED状态 */
            BSP_UART1_Printf(
                "[DHT11] Temp=%2u.%uC (%s) | Hum=%2u.%u%% (%s) | "
                "LED1=%s LED2=%s LED3=%s LED4=%s\r\n",
                dht_data.temperature_int, dht_data.temperature_dec, temp_state,
                dht_data.humidity_int,    dht_data.humidity_dec,    hum_state,
#if BSP_LED_ENABLE
                BSP_LED_GetState(BSP_LED1) ? "ON " : "OFF",
                BSP_LED_GetState(BSP_LED2) ? "ON " : "OFF",
                BSP_LED_GetState(BSP_LED3) ? "ON " : "OFF",
                BSP_LED_GetState(BSP_LED4) ? "ON " : "OFF");
#else
                "NA ", "NA ", "NA ", "NA ");
#endif
        } else {
#if APP_SENSOR_ENABLE
            /* 读取失败：向共享快照标记DHT11异常（保留上一次数值供云端参考） */
            APP_SENSOR_UpdateTempHum(dht_data.temperature_int, dht_data.temperature_dec,
                                     dht_data.humidity_int,    dht_data.humidity_dec,
                                     0U);
#endif
#if APP_OLED_ENABLE && BSP_OLED_ENABLE
            /* 读取失败：OLED 温度行显示错误提示 */
            APP_OLED_ShowError();
#endif
            /* 读取失败：标记快照无效（保留上一次数值） */
            APP_DHT11_UpdateSnapshot(s_snapshot.temperature_int, s_snapshot.temperature_dec,
                                     s_snapshot.humidity_int,    s_snapshot.humidity_dec, 0U);
            /* 读取失败，打印错误信息（含失败阶段，便于定位硬件问题；不改变LED状态） */
            BSP_UART1_Printf(
                "[DHT11] Read FAILED (status=%d, stage=%d), retry=%u\r\n",
                (int)status, (int)BSP_DHT11_GetLastFailStage(), (unsigned)retry);
        }

        /* 7. 任务阻塞2秒，释放CPU */
        osDelay(DHT11_PERIOD_MS);
    }
}

#endif /* APP_DHT11_ENABLE && APP_TASKS_ENABLE && BSP_DHT11_ENABLE */
