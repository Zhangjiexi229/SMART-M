/**
 ******************************************************************************
 * @file    app_qmi8658.c
 * @brief   QMI8658六轴IMU应用任务实现 — 周期采集+姿态解算+共享快照
 *
 *  ▍数据换算：
 *    加速度(raw) -> mg : raw * 1000 / 2048   （±16g，2048 LSB/g）
 *    陀螺仪(raw) -> mdps: raw * 1000 / 256   （±128dps，256 LSB/dps）
 *    pitch/roll 由加速度通过 atan2 计算（静止/缓变工况）
 ******************************************************************************
 */
#include "module_cfg.h"
#include "app_qmi8658.h"
#include "cmsis_os.h"

/* ==========================================================================
 *  QMI8658 采集任务（仅在模块启用时编译）
 * ========================================================================== */
#if APP_QMI8658_ENABLE && APP_TASKS_ENABLE && BSP_QMI8658_ENABLE

#include "bsp_qmi8658.h"
#include "bsp_uart.h"
#if APP_SENSOR_ENABLE
#include "app_sensor.h"
#endif
#if APP_DIAG_ENABLE
#include "app_diag.h"
#endif
#if APP_VIBRATION_ENABLE
#include "app_vibration.h"
#endif
#if BSP_I2C_SOFT_ENABLE
#include "bsp_i2c_soft.h"
#endif
#include <math.h>

/* I2C连续失败计数：>=5次触发总线恢复 */
static uint8_t s_i2c_fail_count = 0U;

/**
 * @brief  计算俯仰角（度，含一位小数拆分）
 * @param  ax, ay, az  加速度原始值（LSB）
 * @param  int_part    输出整数部分（含符号）
 * @param  dec_part    输出小数部分（0~9，恒正）
 */
static void APP_QMI8658_CalcPitch(int16_t ax, int16_t ay, int16_t az,
                                  int16_t *int_part, uint8_t *dec_part)
{
    float pitch_rad;
    float pitch_deg;
    int   pitch_x10;

    pitch_rad = atan2f((float)(-ax), sqrtf((float)ay * ay + (float)az * az));
    pitch_deg = pitch_rad * 57.29578f;   /* 弧度转度 */
    pitch_x10 = (int)(pitch_deg * 10.0f);

    *int_part = (int16_t)(pitch_x10 / 10);
    if (pitch_x10 < 0) {
        *dec_part = (uint8_t)((-pitch_x10) % 10);
    } else {
        *dec_part = (uint8_t)(pitch_x10 % 10);
    }
}

/**
 * @brief  计算横滚角（度，含一位小数拆分）
 */
static void APP_QMI8658_CalcRoll(int16_t ax, int16_t ay, int16_t az,
                                 int16_t *int_part, uint8_t *dec_part)
{
    float roll_rad;
    float roll_deg;
    int   roll_x10;

    (void)ax;
    roll_rad = atan2f((float)ay, (float)az);
    roll_deg = roll_rad * 57.29578f;
    roll_x10 = (int)(roll_deg * 10.0f);

    *int_part = (int16_t)(roll_x10 / 10);
    if (roll_x10 < 0) {
        *dec_part = (uint8_t)((-roll_x10) % 10);
    } else {
        *dec_part = (uint8_t)(roll_x10 % 10);
    }
}

/**
 * @brief  QMI8658六轴IMU采集任务
 * @param  argument 未使用
 */
void APP_QMI8658_Task(void *argument)
{
    (void)argument;

    BSP_QMI8658_Data_t imu;
    uint8_t retry;
    uint8_t ok;
    uint32_t sample_cnt = 0U;
    int16_t  pitch_int, roll_int;
    uint8_t  pitch_dec, roll_dec;
    int16_t  ax_mg, ay_mg, az_mg;
    int16_t  gx_mdps, gy_mdps, gz_mdps;

    /* 任务启动标识 */
    BSP_UART1_Printf("[QMI8658] Task started (period=%ums, acc=+-16g, gyr=+-128dps, addr=0x%02X)\r\n",
                     (unsigned)QMI8658_PERIOD_MS, (unsigned)QMI8658_I2C_ADDR);

    /* QMI8658 上电稳定，首次读取前等待 */
    osDelay(500U);

    for (;;) {
        /* 1. 读取六轴，失败重试 */
        ok = 0U;
        for (retry = 0U; retry < QMI8658_RETRY_COUNT; retry++) {
            if (BSP_QMI8658_Read(&imu) == 0U) {
                ok = 1U;
                break;
            }
            osDelay(50U);
        }

        if (ok != 0U) {
            /* 2. 换算工程值 */
            ax_mg   = (int16_t)(((int32_t)imu.ax * 1000) / QMI8658_ACC_LSB_PER_G);
            ay_mg   = (int16_t)(((int32_t)imu.ay * 1000) / QMI8658_ACC_LSB_PER_G);
            az_mg   = (int16_t)(((int32_t)imu.az * 1000) / QMI8658_ACC_LSB_PER_G);
            gx_mdps = (int16_t)(((int32_t)imu.gx * 1000) / QMI8658_GYR_LSB_PER_DPS);
            gy_mdps = (int16_t)(((int32_t)imu.gy * 1000) / QMI8658_GYR_LSB_PER_DPS);
            gz_mdps = (int16_t)(((int32_t)imu.gz * 1000) / QMI8658_GYR_LSB_PER_DPS);

            /* 3. 姿态角解算 */
            APP_QMI8658_CalcPitch(imu.ax, imu.ay, imu.az, &pitch_int, &pitch_dec);
            APP_QMI8658_CalcRoll(imu.ax, imu.ay, imu.az, &roll_int, &roll_dec);

            /* 4. 更新统一快照（供MQTT上报） */
#if APP_SENSOR_ENABLE
            APP_SENSOR_UpdateIMU(pitch_int, pitch_dec, roll_int, roll_dec,
                                 ax_mg, ay_mg, az_mg, gx_mdps, gy_mdps, gz_mdps,
                                 1U);
#endif

            /* 4b. 更新诊断快照：振动模值 g = sqrt(ax^2+ay^2+az^2)（含静态1g） */
#if APP_DIAG_ENABLE
            {
                float vib_g = sqrtf((float)ax_mg * (float)ax_mg +
                                    (float)ay_mg * (float)ay_mg +
                                    (float)az_mg * (float)az_mg) / 1000.0f;
                APP_DIAG_UpdateVibration(vib_g, 1U);

                /* 4c. 喂入振动特征窗口（10ms/点，100点=1秒窗口，供故障诊断） */
#if APP_VIBRATION_ENABLE
                APP_VIB_GlobalPush(vib_g);
#endif
            }
#endif

            /* 5. 每N次采集打印一次（暂时屏蔽，只留MQTT） */
            sample_cnt++;
            // if ((sample_cnt % QMI8658_PRINT_DIV) == 0U) {
            //     BSP_UART1_Printf(
            //         "[QMI8658] pitch=%d.%u roll=%d.%u | A=(%+d,%+d,%+d)mg | G=(%+d,%+d,%+d)mdps\r\n",
            //         (int)pitch_int, pitch_dec, (int)roll_int, roll_dec,
            //         (int)ax_mg, (int)ay_mg, (int)az_mg,
            //         (int)gx_mdps, (int)gy_mdps, (int)gz_mdps);
            // }
        } else {
#if APP_SENSOR_ENABLE
            /* 读取失败：标记快照无效（保留上一次数值） */
            APP_SENSOR_UpdateIMU(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0U);
#endif
#if APP_DIAG_ENABLE
            APP_DIAG_UpdateVibration(0.0f, 0U);
#endif
            BSP_UART1_Printf("[QMI8658] Read FAILED, retry=%u (check I2C wiring/addr=0x%02X)\r\n",
                             (unsigned)retry, (unsigned)QMI8658_I2C_ADDR);

            /* I2C连续失败：触发总线恢复 */
#if BSP_I2C_SOFT_ENABLE
            s_i2c_fail_count++;
            if (s_i2c_fail_count >= 5U) {
                BSP_UART1_Printf("[QMI8658] I2C failed %u times, bus recovering...\r\n",
                                 (unsigned)s_i2c_fail_count);
                BSP_I2C_Soft_Recover();
                s_i2c_fail_count = 0U;
            }
#endif
        }

        /* 6. 任务阻塞，释放CPU */
        osDelay(QMI8658_PERIOD_MS);
    }
}

#endif /* APP_QMI8658_ENABLE && APP_TASKS_ENABLE && BSP_QMI8658_ENABLE */
