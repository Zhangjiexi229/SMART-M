/**
 ******************************************************************************
 * @file    app_sd.c
 * @brief   SD 卡数据存储应用实�? *
 *  任务流程�? *    1. 初始�?SDIO（失败重�?3 次） -> 打印容量�? *    2. f_mount 挂载 FAT 卷（失败�?10s 重试）；
 *    3. 挂载成功后打开 /SMART/LOGxxxx.CSV（续写或新建，新建写表头）；
 *    4. �?5s 读取诊断快照写一�?CSV �?f_sync�? *    5. 单文件超�?256KB 自动滚动到下一个编号；
 *    6. 写失败（卡拔出等）自动关闭文件、进入重试挂载流程�? ******************************************************************************
 */
#include "module_cfg.h"
#if APP_SD_ENABLE && APP_TASKS_ENABLE && BSP_SD_ENABLE

#include "app_sd.h"
#include "bsp_sd.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"   /* xPortGetFreeHeapSize */
#include "ff.h"
#include "main.h"
#if BSP_UART1_ENABLE
#include "bsp_uart.h"
#endif
#if APP_WATCHDOG_ENABLE
#include "app_watchdog.h"
#endif
#if APP_DIAG_ENABLE
#include "app_diag.h"
#endif
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ==========================================================================
 *  配置
 * ========================================================================== */
#define APP_SD_LOG_PERIOD_MS    5000U     /* 数据落盘周期 */
#define APP_SD_MOUNT_RETRY_MS   10000U    /* 挂载失败重试间隔 */
#define APP_SD_INIT_RETRY       3U        /* SDIO 初始化重试次�?*/
#define APP_SD_LOG_FILE_MAX     262144U   /* 单个日志文件上限 256KB */
#define APP_SD_MAX_LINE         220U      /* 单行最大长�?*/
#define APP_SD_DIR              "0:/SMART"
#define APP_SD_FILE_PREFIX      "LOG"
#define APP_SD_FILE_SUFFIX      ".CSV"

/* ==========================================================================
 *  内部状�? * ========================================================================== */
static FATFS           s_fs;
static FIL             s_file;
static char            s_file_name[32];
static uint8_t         s_file_open = 0U;
static volatile uint8_t s_ready    = 0U;
static osMutexId_t     s_sd_mutex;

/* ==========================================================================
 *  内部函数
 * ========================================================================== */
static void SD_Lock(void)
{
    if (s_sd_mutex != NULL) {
        (void)osMutexAcquire(s_sd_mutex, osWaitForever);
    }
}

static void SD_Unlock(void)
{
    if (s_sd_mutex != NULL) {
        (void)osMutexRelease(s_sd_mutex);
    }
}

/* 打开下一个编号的日志文件（已存在则追加续写，返回 0 成功�?*/
static uint8_t SD_OpenNextLogFile(void)
{
    DIR dir;
    FILINFO finfo;
    FRESULT fr;
    uint16_t max_no = 0U;
    uint16_t no;

    /* 确保目录存在 */
    fr = f_mkdir(APP_SD_DIR);
    if ((fr != FR_OK) && (fr != FR_EXIST)) {
        BSP_UART1_Printf("[SD] mkdir fail fr=%d\r\n", (int)fr);
        return 1U;
    }

    /* 扫描已有 LOGxxxx.CSV，取最大编�?*/
    fr = f_opendir(&dir, APP_SD_DIR);
    if (fr != FR_OK) {
        return 1U;
    }
    for (;;) {
        fr = f_readdir(&dir, &finfo);
        if ((fr != FR_OK) || (finfo.fname[0] == '\0')) {
            break;
        }
        if (sscanf(finfo.fname, APP_SD_FILE_PREFIX "%4hu" APP_SD_FILE_SUFFIX, &no) == 1) {
            if (no > max_no) {
                max_no = no;
            }
        }
    }
    (void)f_closedir(&dir);

    no = (uint16_t)(max_no + 1U);
    if (no > 9999U) {
        no = 1U;   /* 编号回绕 */
    }
    (void)snprintf(s_file_name, sizeof(s_file_name),
                   APP_SD_DIR "/" APP_SD_FILE_PREFIX "%04hu" APP_SD_FILE_SUFFIX, no);

    fr = f_open(&s_file, s_file_name, FA_OPEN_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        BSP_UART1_Printf("[SD] fopen fail fr=%d\r\n", (int)fr);
        return 1U;
    }
    /* 新文件（空文件）�?CSV 表头 */
    if (f_size(&s_file) == 0U) {
        const char *hdr = "uptime_s,temp_c,hum_rh,vib_g,current_a,voltage_v,power_w,alarm\r\n";
        UINT bw = 0U;
        (void)f_write(&s_file, hdr, (UINT)strlen(hdr), &bw);
        (void)f_sync(&s_file);
    }
    return 0U;
}

/* 追加一行到日志文件（调用方需已持有互斥锁�?*/
static uint8_t SD_WriteLineLocked(const char *line)
{
    FRESULT fr;
    UINT bw;
    size_t len = strlen(line);

    if ((s_ready == 0U) || (s_file_open == 0U)) {
        return 1U;
    }

    /* 单文件超�?-> 滚动新建 */
    if (f_size(&s_file) > APP_SD_LOG_FILE_MAX) {
        (void)f_close(&s_file);
        s_file_open = 0U;
        if (SD_OpenNextLogFile() != 0U) {
            s_ready = 0U;
            return 1U;
        }
        s_file_open = 1U;
    }

    fr = f_write(&s_file, line, (UINT)len, &bw);
    if ((fr != FR_OK) || (bw != len)) {
        /* 写失败：卡拔�?写保护等 -> 关闭并进入重挂载流程 */
        (void)f_close(&s_file);
        s_file_open = 0U;
        s_ready = 0U;
        return 1U;
    }
    (void)f_sync(&s_file);   /* 每条落盘，掉电不丢数�?*/
    return 0U;
}

/* ==========================================================================
 *  公共 API
 * ========================================================================== */
uint8_t APP_SD_IsReady(void)
{
    return s_ready;
}

uint8_t APP_SD_Format(void)
{
    static uint8_t s_work[512] __attribute__((aligned(4)));
    FRESULT fr;
    uint8_t ret = 1U;

    SD_Lock();
    if (s_file_open) {
        (void)f_close(&s_file);
        s_file_open = 0U;
    }
    s_ready = 0U;

    fr = f_mount(NULL, "0:", 1U);           /* 先卸�?*/
    if (fr == FR_OK) {
        fr = f_mkfs("0:", 0U, 0U, s_work, sizeof(s_work));
    }
    if (fr == FR_OK) {
        fr = f_mount(&s_fs, "0:", 1U);      /* 重新挂载 */
    }
    if (fr == FR_OK) {
        ret = 0U;
    }
    SD_Unlock();
    return ret;
}

uint8_t APP_SD_AppendLine(const char *line)
{
    uint8_t ret;

    if (line == NULL) {
        return 1U;
    }
    SD_Lock();
    ret = SD_WriteLineLocked(line);
    SD_Unlock();
    return ret;
}

/* ==========================================================================
 *  任务
 * ========================================================================== */
void APP_SD_Task(void *argument)
{
    (void)argument;
    BSP_SD_CardInfo_t info;
    uint8_t init_ok = 0U;
    uint8_t i;

#if APP_WATCHDOG_ENABLE
    /* 初始化（无卡�?HAL_SD_Init 重试可能耗时 >1s），先喂一次避免误复位 */
    Watchdog_Kick(WDT_TASK_SD);
#endif
    /* 任务已成功创建：打印剩余堆，便于确认堆是否紧�?*/
    BSP_UART1_Printf("[SD] task running, free heap=%lu B\r\n",
                     (unsigned long)xPortGetFreeHeapSize());
    s_sd_mutex = osMutexNew(NULL);

    /* 1. 初始�?SDIO（可重试�?*/
    for (i = 0U; i < APP_SD_INIT_RETRY; i++) {
#if APP_WATCHDOG_ENABLE
        Watchdog_Kick(WDT_TASK_SD);   /* BSP_SD_Init 内部最长可能忙等数秒，先喂�?*/
#endif
        if (BSP_SD_Init() == 0U) {
            init_ok = 1U;
            break;
        }
#if APP_WATCHDOG_ENABLE
        Watchdog_DelayWithKick(WDT_TASK_SD, 500U);
#else
        osDelay(500U);
#endif
    }
    if (init_ok != 0U) {
        if (BSP_SD_GetCardInfo(&info) == 0U) {
            BSP_UART1_Printf("[SD] card ok: %lu MB\r\n",
                             (unsigned long)(info.capacity_bytes / (1024UL * 1024UL)));
        } else {
            BSP_UART1_Printf("[SD] card ok\r\n");
        }
    } else {
        BSP_UART1_Printf("[SD] init fail, retry later\r\n");
    }

    for (;;) {
#if APP_WATCHDOG_ENABLE
        Watchdog_Kick(WDT_TASK_SD);
#endif

        /* 2. 未就绪：尝试挂载 */
        if (s_ready == 0U) {
            static uint32_t s_last_notify = 0U;
            static FRESULT  s_last_fr = FR_OK;   /* 上次失败错误码（30s 状态打印带出） */
            uint32_t now;

            if (init_ok != 0U) {
#if APP_WATCHDOG_ENABLE
                Watchdog_Kick(WDT_TASK_SD);   /* f_mount 内部可能忙等读卡，先喂一次避免被判死 */
#endif
                uint32_t t0 = HAL_GetTick();
                FRESULT fr = f_mount(&s_fs, "0:", 1U);
                BSP_UART1_Printf("[SD] mount fr=%d t=%lu ms\r\n", (int)fr,
                                 (unsigned long)(HAL_GetTick() - t0));
                if (fr == FR_OK) {
#if APP_WATCHDOG_ENABLE
                    Watchdog_Kick(WDT_TASK_SD);   /* 开文件/写FAT前再喂一次，避免写扇区忙等拖垮心�?*/
#endif
                    if (SD_OpenNextLogFile() == 0U) {
                        s_file_open = 1U;
                        s_ready = 1U;
                        BSP_UART1_Printf("[SD] mounted, log=%s\r\n", s_file_name);
                    } else {
                        s_last_fr = FR_INVALID_NAME;   /* 建目�?开文件失败 */
                        BSP_UART1_Printf("[SD] open log fail (check card write-protect & FAT32)\r\n");
                        (void)f_mount(NULL, "0:", 0U);
                    }
                } else {
                    s_last_fr = fr;
                    (void)f_mount(NULL, "0:", 0U);
                }
            } else {
                /* 卡初始化失败：每 10s 重试初始�?*/
                if (BSP_SD_Init() == 0U) {
                    init_ok = 1U;
                }
            }

            /* �?30s 打印一次状态：带错误码便于定位
             * fr: 1=磁盘读错�?高�?接触/上拉), 3=卡未就绪, 13=非FAT文件系统 */
            now = HAL_GetTick();
            if ((now - s_last_notify) >= 30000U) {
                s_last_notify = now;
                BSP_UART1_Printf("[SD] not ready, retrying... (fr=%d, check card & FAT32)\r\n",
                                 (int)s_last_fr);
            }

            /* 喂狗式延时：IWDG 超时�?3s，不能整�?10s 静默 */
            #if APP_WATCHDOG_ENABLE
            Watchdog_DelayWithKick(WDT_TASK_SD, APP_SD_MOUNT_RETRY_MS);
#else
            osDelay(APP_SD_MOUNT_RETRY_MS);
#endif
            continue;
        }

        /* 3. 周期写一行诊断数�?*/
#if APP_DIAG_ENABLE
        {
            APP_Diag_Snapshot_t snap;
            char line[APP_SD_MAX_LINE];

            APP_DIAG_GetSnapshot(&snap);
            (void)snprintf(line, sizeof(line),
                           "%lu,%.1f,%.1f,%.3f,%.3f,%.2f,%.3f,%u\r\n",
                           (unsigned long)(HAL_GetTick() / 1000UL),
                           (double)snap.temperature,
                           (double)snap.humidity,
                           (double)snap.vibration,
                           (double)snap.current,
                           (double)snap.voltage,
                           (double)snap.power,
                           (unsigned int)g_alarm_active);
            (void)APP_SD_AppendLine(line);
        }
#endif /* APP_DIAG_ENABLE */

        /* 4. 带心跳延时（�?500ms kick 一次） */
#if APP_WATCHDOG_ENABLE
        Watchdog_DelayWithKick(WDT_TASK_SD, APP_SD_LOG_PERIOD_MS);
#else
        osDelay(APP_SD_LOG_PERIOD_MS);
#endif
    }
}

#endif /* APP_SD_ENABLE && APP_TASKS_ENABLE && BSP_SD_ENABLE */
