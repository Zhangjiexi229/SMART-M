/**
 ******************************************************************************
 * @file    app_hc_sr04.c
 * @brief   HC-SR04 超声波测距应用层实现 — 周期采集任务
 *
 *  任务流程：
 *    循环 {
 *      从RAM配置镜像读取 period/threshold/enable（零开销，非 EEPROM 读）
 *      if (enable) {
 *        BSP_HC_SR04_MeasureMm() →阻塞测量(最长30ms)
 *        更新快照 s_snapshot
 *        串口打印 [Ultrasonic] Dist=xxxmm
 *        if (APP_HC_SR04_LED_ENABLE) 距离<threshold → 点亮LED1
 *      }
 *      osDelay(period)
 *    }
 *
 *  配置来源：app_config_t 中的 ultrasonic_period_ms / ultrasonic_threshold_mm / ultrasonic_enable
 *  这些字段由十六进制协议寄存器(0x0250~0x0252)在线修改并保存到 EEPROM
 ******************************************************************************
 */
#include "module_cfg.h"
#if APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE

#include "app_hc_sr04.h"
#include "bsp_hc_sr04.h"
#include "bsp_uart.h"
#include "cmsis_os.h"

#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
#include "app_eeprom.h"
#endif

#if BSP_LED_ENABLE
#include "bsp_led.h"
#endif

/* ========== 默认配置（EEPROM 未初始化时的回退值） ========== */
#define HC_SR04_DEFAULT_PERIOD_MS      500U    /* 默认测量周期500ms */
#define HC_SR04_DEFAULT_THRESHOLD_MM   300U    /* 默认报警阈值300mm */

/* ========== 最新数据快照 ========== */
static HC_SR04_Snapshot_t s_snapshot = {0};

/* ========== 公共 API ========== */

void APP_HC_SR04_GetSnapshot(HC_SR04_Snapshot_t *snap)
{
    if (snap != NULL) {
        *snap = s_snapshot;   /* 结构体拷贝（8字节，原子性足够） */
    }
}

/* ========== 任务实现 ========== */
void APP_HC_SR04_Task(void *argument)
{
    (void)argument;
    uint16_t dist_mm;
    uint8_t  st;

    BSP_UART1_Printf("[Ultrasonic] Task started\r\n");

    for (;;) {
        /* ---- 从 RAM 配置镜像读取参数（零开销）---- */
        uint16_t period_ms    = HC_SR04_DEFAULT_PERIOD_MS;
        uint8_t  enable       = 1U;
#if APP_HC_SR04_LED_ENABLE && BSP_LED_ENABLE
        uint16_t threshold_mm = HC_SR04_DEFAULT_THRESHOLD_MM;
#endif

#if APP_EEPROM_ENABLE && BSP_AT24C02_ENABLE
        {
            const app_config_t *cfg = APP_EEPROM_GetConfig();
            if (cfg->ultrasonic_period_ms >= 100U) {
                period_ms = cfg->ultrasonic_period_ms;   /* 最小100ms */
            }
#if APP_HC_SR04_LED_ENABLE && BSP_LED_ENABLE
            if (cfg->ultrasonic_threshold_mm >= HC_SR04_MIN_DIST_MM) {
                threshold_mm = cfg->ultrasonic_threshold_mm;
            }
#endif
            enable = cfg->ultrasonic_enable;
        }
#endif

        if (enable) {
            /* ---- 触发一次测距（阻塞，最长约30ms）---- */
            st = BSP_HC_SR04_MeasureMm(&dist_mm);

            if (st == 0U) {
                /* 测量成功：更新快照 */
                s_snapshot.distance_mm   = dist_mm;
                s_snapshot.valid         = 1U;
                s_snapshot.measure_count++;

                BSP_UART1_Printf("[Ultrasonic] Dist=%umm\r\n", (unsigned)dist_mm);

                /* ---- 可选：距离阈值控制 LED1 ---- */
#if APP_HC_SR04_LED_ENABLE && BSP_LED_ENABLE
                if (dist_mm < threshold_mm) {
                    BSP_LED_On(BSP_LED1);
                } else {
                    BSP_LED_Off(BSP_LED1);
                }
#endif
            } else {
                /* 测量超时：标记无效 */
                s_snapshot.valid = 0U;
                BSP_UART1_Printf("[Ultrasonic] Measure timeout (no echo)\r\n");
            }
        }

        /* ---- 等待下一周期 ---- */
        osDelay(period_ms);
    }
}

#endif /* APP_HC_SR04_ENABLE && BSP_HC_SR04_ENABLE */
