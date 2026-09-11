/**
 ******************************************************************************
 * @file    app_light_sensor.c
 * @brief   光敏传感器应用任务实现 — 周期采集 + 串口打印 + LED分级控制 + OLED显示
 *
 *  任务流程：
 *    1. BSP_LightSensor_ReadRaw() 读取12位ADC值
 *    2. 换算电压(mV)
 *    3. APP_LightSensor_Classify() 按阈值判定光照等级
 *    4. APP_LightSensor_ControlLED() 按等级控制LED1/LED2
 *    5. APP_OLED_UpdateLight() 光照ADC值和等级显示到 OLED
 *    6. BSP_UART1_Printf() 打印采集数据和状态
 *    7. 更新共享快照 s_snapshot（供WiFi任务读取上报）
 *    8. osDelay(500ms) 进入阻塞，释放CPU
 *
 *  光照等级与LED对应表：
 *    等级      ADC范围      LED1(PG14)  LED2(PG13)
 *    DARK      >= 500       亮           亮
 *    MEDIUM    200 ~ 499    灭           亮
 *    BRIGHT    < 200        灭           灭
 *
 *  LED控制受 BSP_LED_ENABLE && APP_LIGHT_LED_ENABLE 条件编译保护。
 ******************************************************************************
 */
#include "module_cfg.h"
#include "app_light_sensor.h"
#include "cmsis_os.h"

/* ==========================================================================
 *  共享快照（始终可用，不依赖 APP_LIGHT_SENSOR_ENABLE）
 *  光敏任务写，WiFi任务读，临界区保护
 * ========================================================================== */
static LightSensor_Snapshot_t s_snapshot = {0};

void APP_LightSensor_GetSnapshot(LightSensor_Snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL();
    *out = s_snapshot;
    taskEXIT_CRITICAL();
}

/* ==========================================================================
 *  光敏采集任务（仅在模块启用时编译）
 * ========================================================================== */
#if APP_LIGHT_SENSOR_ENABLE && APP_TASKS_ENABLE && BSP_LIGHT_SENSOR_ENABLE

#include "bsp_light_sensor.h"
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

typedef uint8_t LightLevel_t;

/**
 * @brief  更新共享快照（内部使用，临界区保护）
 */
static void APP_LightSensor_UpdateSnapshot(uint16_t adc, uint16_t mv, uint8_t level, uint8_t valid)
{
    taskENTER_CRITICAL();
    s_snapshot.adc_value  = adc;
    s_snapshot.voltage_mv = mv;
    s_snapshot.level      = level;
    s_snapshot.valid      = valid;
    taskEXIT_CRITICAL();
}

/**
 * @brief  根据ADC值判定光照等级
 * @param  adc_value 12位ADC原始值
 * @return 光照等级
 */
static LightLevel_t APP_LightSensor_Classify(uint16_t adc_value)
{
    if (adc_value >= LIGHT_TH_HIGH) {
        return LIGHT_LEVEL_DARK;
    } else if (adc_value >= LIGHT_TH_LOW) {
        return LIGHT_LEVEL_MEDIUM;
    } else {
        return LIGHT_LEVEL_BRIGHT;
    }
}

/**
 * @brief  按光照等级控制LED1/LED2
 * @param  level 光照等级
 */
static void APP_LightSensor_ControlLED(LightLevel_t level)
{
#if BSP_LED_ENABLE && APP_LIGHT_LED_ENABLE
    switch (level) {
        case LIGHT_LEVEL_DARK:
            /* 暗光：双灯全亮，补光指示 */
            BSP_LED_On(BSP_LED1);
            BSP_LED_On(BSP_LED2);
            break;

        case LIGHT_LEVEL_MEDIUM:
            /* 中等光：仅LED2亮，过渡指示 */
            BSP_LED_Off(BSP_LED1);
            BSP_LED_On(BSP_LED2);
            break;

        case LIGHT_LEVEL_BRIGHT:
        default:
            /* 强光：双灯全灭，节能 */
            BSP_LED_Off(BSP_LED1);
            BSP_LED_Off(BSP_LED2);
            break;
    }
#else
    (void)level;   /* LED驱动或光敏LED控制未启用，不操作LED */
#endif
}

/**
 * @brief  光敏传感器任务 — 周期采集、判定、控灯、OLED显示、打印
 * @param  argument 未使用
 */
void APP_LightSensor_Task(void *argument)
{
    (void)argument;

    uint16_t      adc_value;
    uint32_t      voltage_mv;
    LightLevel_t  level;
    const char   *level_str;

    for (;;) {
        /* 1. 采集ADC原始值 */
        adc_value = BSP_LightSensor_ReadRaw();

        /* 2. 换算电压（整数mV，避免浮点库） */
        voltage_mv = (uint32_t)adc_value * LIGHT_SENSOR_VREF_MV
                     / LIGHT_SENSOR_ADC_MAX_VALUE;

#if APP_SENSOR_ENABLE
        /* 发布到共享传感器快照（供WiFi云端上报消费） */
        APP_SENSOR_UpdateLight(adc_value, (uint16_t)voltage_mv);
#endif

        /* 3. 判定光照等级 */
        level = APP_LightSensor_Classify(adc_value);

        /* 4. 按等级控制LED */
        APP_LightSensor_ControlLED(level);

        /* 5. 等级字符串映射 */
        switch (level) {
            case LIGHT_LEVEL_DARK:   level_str = "DARK  "; break;
            case LIGHT_LEVEL_MEDIUM: level_str = "MEDIUM"; break;
            case LIGHT_LEVEL_BRIGHT: level_str = "BRIGHT"; break;
            default:                  level_str = "UNKNOWN"; break;
        }

#if APP_OLED_ENABLE && BSP_OLED_ENABLE
        /* 6. 光照ADC值和等级显示到 OLED（内部互斥锁，防与DHT11任务并发写屏） */
        APP_OLED_UpdateLight(adc_value, level_str);
#endif

        /* 7. 更新共享快照（供WiFi任务读取上报） */
        APP_LightSensor_UpdateSnapshot(adc_value, (uint16_t)voltage_mv, (uint8_t)level, 1U);

        /* 8. 串口打印采集数据和LED状态 */
        BSP_UART1_Printf(
            "[Light] ADC=%4u | %4umV | Level=%s | LED1=%s | LED2=%s\r\n",
            adc_value,
            (unsigned)voltage_mv,
            level_str,
#if BSP_LED_ENABLE
            BSP_LED_GetState(BSP_LED1) ? "ON " : "OFF",
            BSP_LED_GetState(BSP_LED2) ? "ON " : "OFF");
#else
            "NA ", "NA ");
#endif

        /* 9. 任务阻塞，释放CPU给其他任务 */
        osDelay(LIGHT_SENSOR_PERIOD_MS);
    }
}

#endif /* APP_LIGHT_SENSOR_ENABLE && APP_TASKS_ENABLE && BSP_LIGHT_SENSOR_ENABLE */
