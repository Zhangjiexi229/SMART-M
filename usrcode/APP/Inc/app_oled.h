/**
 ******************************************************************************
 * @file    app_oled.h
 * @brief   OLED显示应用层 — 毕设三页面（实时/阈值/状态），互斥锁保护
 *
 *  对应工作任务 Day3「OLED三路显示」Day5「OLED告警显示优化」：
 *    - 实时页（默认）：Motor Monitor + Temp/Vib/Curr 三路数据 + 诊断状态行
 *    - 阈值页：三路阈值列表，设置模式下选中项前加 ">" 高亮
 *    - 状态页：WiFi / 继电器 / 告警状态
 *    - 告警时整屏反白闪烁（0xA7/0xA6 硬件反色，每500ms切换）
 *
 *  并发保护：
 *    所有写操作在互斥锁保护下进行；DisplayTask 独占屏幕刷新，
 *    旧接口（UpdateTempHum/UpdateLight/ShowError）保留签名但不再绘制，
 *    兼容 DHT11/光敏任务的调用（避免双写花屏）。
 ******************************************************************************
 */
#ifndef APP_OLED_H
#define APP_OLED_H

#include <stdint.h>

/* ========== 页面枚举 ========== */
typedef enum {
    APP_OLED_PAGE_REALTIME  = 0,   /* 实时数据 */
    APP_OLED_PAGE_THRESHOLD,       /* 阈值设置 */
    APP_OLED_PAGE_STATUS,          /* 系统状态 */
    APP_OLED_PAGE_MAX
} APP_OLED_Page_t;

/**
 * @brief  创建 OLED 写互斥锁（在调度器启动前、RTOS对象创建阶段调用）
 */
void APP_OLED_CreateMutex(void);

/**
 * @brief  OLED 硬件初始化 + 清屏（在 APP_Init 中调用，调度器启动前）
 */
void APP_OLED_Init(void);

/**
 * @brief  切换页面（A键触发：实时->阈值->状态->实时 循环）
 */
void APP_OLED_PageSwitch(void);

/**
 * @brief  直接跳转到指定页面
 * @param  page  APP_OLED_PAGE_*
 */
void APP_OLED_ShowPage(APP_OLED_Page_t page);

/**
 * @brief  显示任务 — 100ms周期：按当前页渲染 + 告警反白闪烁
 * @param  argument 未使用（FreeRTOS入口）
 */
void APP_OLED_DisplayTask(void *argument);

/* ==========================================================================
 *  兼容旧调用（DHT11/光敏任务）：DisplayTask 接管屏幕后这些函数不再绘制
 * ========================================================================== */
void APP_OLED_UpdateTempHum(uint8_t temp_int, uint8_t temp_dec,
                            uint8_t hum_int,  uint8_t hum_dec);
void APP_OLED_UpdateLight(uint16_t adc_value, const char *level_str);
void APP_OLED_ShowError(void);

#endif /* APP_OLED_H */
