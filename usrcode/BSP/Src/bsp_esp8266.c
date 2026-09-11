/**
 ******************************************************************************
 * @file    bsp_esp8266.c
 * @brief   ESP8266 WiFi 模块 AT 指令驱动实现
 *
 *  实现策略：
 *    1. 发送：通过 BSP_UART3_Send() 把 AT 指令（自动追加 \r\n）发给模块
 *    2. 接收：轮询 BSP_UART3_Read() 从软件环形缓冲取数据，累积到内部响应缓冲
 *    3. 匹配：在响应缓冲中搜索期望关键字（如 OK / CONNECT / SEND OK）
 *       或失败关键字（如 ERROR / FAIL），命中即返回
 *    4. 超时：循环内 HAL_Delay(10ms) 等待，累计等待时间超过阈值则超时
 *
 *  注意：
 *    - 所有函数均为阻塞式，BSP 层使用 HAL_Delay（RTOS 无关），
 *      可在调度器启动前的 APP_Init() 中调用，也可在任务上下文中调用
 *    - ESP8266 默认波特率 115200，与 USART3 配置一致
 *    - 响应缓冲 512 字节，覆盖绝大多数 AT 响应
 *    - 调试：超时后会把 UART3 收到的原始数据打印到 UART1，便于排查
 *      接线/供电/波特率问题
 ******************************************************************************
 */
#include "module_cfg.h"

#if BSP_ESP8266_ENABLE

#include "bsp_esp8266.h"
#include "bsp_uart.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

/* ========== 内部配置 ========== */
#define ESP8266_RESP_BUF_SIZE     512U    /*!< 响应累积缓冲大小 */
#define ESP8266_POLL_INTERVAL_MS  10U     /*!< 轮询间隔（ms），同时作为超时计数步长 */
#define ESP8266_CMD_TIMEOUT_MS    2000U   /*!< 普通 AT 指令默认超时 */
#define ESP8266_CIFSR_TIMEOUT_MS  5000U   /*!< 查询 IP 超时（WiFi刚连上时模块响应慢） */
#define ESP8266_JOIN_TIMEOUT_MS   15000U  /*!< 连接路由器超时（DHCP 较慢） */
#define ESP8266_TCP_TIMEOUT_MS    8000U   /*!< TCP 连接超时 */
#define ESP8266_SEND_TIMEOUT_MS   5000U   /*!< 数据发送超时 */
#define ESP8266_READY_DELAY_MS    2000U   /*!< 上电后等待模块就绪 */

/* ========== 内部资源 ========== */
static uint8_t  s_resp_buf[ESP8266_RESP_BUF_SIZE];  /*!< 响应累积缓冲（AT响应） */
static uint32_t s_resp_len;                          /*!< 响应缓冲已用长度 */

/* +IPD 提取：从 AT 响应流中分离出 TCP 接收数据，供远程控灯等应用使用 */
#define ESP8266_IPD_BUF_SIZE  128U
static uint8_t  s_ipd_buf[ESP8266_IPD_BUF_SIZE];  /*!< +IPD 提取出的纯数据 */
static uint32_t s_ipd_len;                         /*!< 已提取数据长度 */

/* MQTT 模式：1=使用原始数据通道（不追加\n），0=文本模式（旧PollIPD，追加\n） */
static uint8_t  s_mqtt_mode = 0U;
/* MQTT 专用接收环形缓冲（二进制安全，不追加\n） */
#define ESP8266_MQTT_BUF_SIZE  512U
static uint8_t  s_mqtt_buf[ESP8266_MQTT_BUF_SIZE];
static uint32_t s_mqtt_head;   /* 读指针 */
static uint32_t s_mqtt_tail;   /* 写指针 */

/* ==========================================================================
 *  内部辅助函数
 * ========================================================================== */

static void esp8266_scan_ipd(void);   /* +IPD 包扫描与数据提取（在 pump_rx 中调用） */

/**
 * @brief  把当前响应缓冲的原始数据以十六进制+字符串形式打印到 UART1（调试用）
 */
static void esp8266_dump_rx(const char *tag)
{
#if BSP_UART1_ENABLE
    uint32_t i;
    uint32_t avail = BSP_UART3_Available();

    BSP_UART1_Printf("[ESP8266-DBG] %s: ring_avail=%u, resp_len=%u\r\n",
                     tag, (unsigned)avail, (unsigned)s_resp_len);

    if (s_resp_len > 0U) {
        /* 先打印十六进制 */
        BSP_UART1_Printf("[ESP8266-DBG] HEX:");
        for (i = 0U; (i < s_resp_len) && (i < 64U); i++) {
            BSP_UART1_Printf(" %02X", s_resp_buf[i]);
        }
        if (s_resp_len > 64U) {
            BSP_UART1_Printf(" ...(truncated, total %u bytes)", (unsigned)s_resp_len);
        }
        BSP_UART1_Printf("\r\n");

        /* 再打印可打印字符（不可打印字符显示为 '.'） */
        BSP_UART1_Printf("[ESP8266-DBG] ASC:");
        for (i = 0U; (i < s_resp_len) && (i < 64U); i++) {
            char c = (char)s_resp_buf[i];
            if ((c >= 0x20) && (c < 0x7F)) {
                BSP_UART1_Printf("%c", c);
            } else {
                BSP_UART1_Printf(".");
            }
        }
        BSP_UART1_Printf("\r\n");
    }
#else
    (void)tag;
#endif /* BSP_UART1_ENABLE */
}

/**
 * @brief  清空 UART3 接收链路（DMA缓冲 + 软件环形缓冲 + 内部响应缓冲）
 */
static void esp8266_flush_rx(void)
{
    uint8_t tmp[64];
    /* 把软件环形缓冲里的残留数据全部读走丢弃 */
    while (BSP_UART3_Available() > 0U) {
        (void)BSP_UART3_Read(tmp, sizeof(tmp));
    }
    s_resp_len = 0U;
    memset(s_resp_buf, 0, sizeof(s_resp_buf));
    s_ipd_len = 0U;
    memset(s_ipd_buf, 0, sizeof(s_ipd_buf));
}

/**
 * @brief  从 UART3 读取新数据，追加到内部响应缓冲
 * @retval 本次新读到的字节数
 */
static uint32_t esp8266_pump_rx(void)
{
    uint32_t avail = BSP_UART3_Available();
    uint32_t free_space;
    uint32_t to_read;
    uint32_t got;

    if (avail == 0U) {
        return 0U;
    }

    if (s_resp_len >= ESP8266_RESP_BUF_SIZE) {
        /* 缓冲满：丢弃最旧的一半，给新数据腾空间 */
        uint32_t half = ESP8266_RESP_BUF_SIZE / 2U;
        memmove(s_resp_buf, &s_resp_buf[half], ESP8266_RESP_BUF_SIZE - half);
        s_resp_len = ESP8266_RESP_BUF_SIZE - half;
    }

    free_space = ESP8266_RESP_BUF_SIZE - s_resp_len;
    to_read = (avail < free_space) ? avail : free_space;
    got = BSP_UART3_Read(&s_resp_buf[s_resp_len], to_read);
    s_resp_len += got;

    /* 从新累积的 AT 响应流中扫描并提取 +IPD 数据（TCP 接收）
     * MQTT模式下不调用旧的scan_ipd（会追加\n破坏二进制），由TCPRead内部用scan_ipd_raw处理 */
    if (s_mqtt_mode == 0U) {
        esp8266_scan_ipd();
    }

    return got;
}

/**
 * @brief  在 s_resp_buf 中扫描 +IPD 包，提取纯数据到 s_ipd_buf，并从响应缓冲移除
 *
 *  注意：本项目使用 AT+CIPMUX=0 单连接模式，+IPD 格式为：
 *    +IPD,<data_len>:<data>
 *  例如：+IPD,6:LED1ON  → 提取 "LED1ON"（6字节）
 *  （没有 link_id 字段；多连接模式才是 +IPD,<link_id>,<len>:<data>）
 *
 *  不完整的包（头部到达但数据未到齐）保留在 s_resp_buf 中等待后续数据。
 */
static void esp8266_scan_ipd(void)
{
    uint32_t i = 0U;

    while (i < s_resp_len) {
        /* 搜索 "+IPD," 前缀 */
        if ((s_resp_buf[i] == '+') &&
            (i + 4U < s_resp_len) &&
            (s_resp_buf[i + 1U] == 'I') &&
            (s_resp_buf[i + 2U] == 'P') &&
            (s_resp_buf[i + 3U] == 'D') &&
            (s_resp_buf[i + 4U] == ',')) {

            /* 单连接模式：+IPD,<data_len>:<data>
             * pos 直接指向 data_len 的第一个数字（无需跳过 link_id） */
            uint32_t pos = i + 5U;

            /* 解析 data_len（到冒号） */
            uint32_t len_start = pos;
            while ((pos < s_resp_len) && (s_resp_buf[pos] != ':')) {
                pos++;
            }
            if (pos >= s_resp_len) {
                break;   /* 包不完整 */
            }

            uint32_t data_len = 0U;
            for (uint32_t j = len_start; j < pos; j++) {
                if ((s_resp_buf[j] >= '0') && (s_resp_buf[j] <= '9')) {
                    data_len = data_len * 10U + (uint32_t)(s_resp_buf[j] - '0');
                }
            }
            pos++;   /* 跳过冒号 */

            /* 检查数据是否完整到达 */
            if (pos + data_len > s_resp_len) {
                break;   /* 数据未到齐，等待 */
            }

            /* 提取数据到 s_ipd_buf（溢出部分丢弃） */
            uint32_t copy_len = data_len;
            if (copy_len > (sizeof(s_ipd_buf) - s_ipd_len)) {
                copy_len = sizeof(s_ipd_buf) - s_ipd_len;
            }
            if (copy_len > 0U) {
                memcpy(&s_ipd_buf[s_ipd_len], &s_resp_buf[pos], copy_len);
                s_ipd_len += copy_len;
            }

            /* 追加换行符作为指令分隔符：确保多个 +IPD 包的数据能被
             * 协议解析器区分（否则 "ALLON"+"ALLOFF" 会连成 "ALLONALLOFF" 匹配失败）。
             * 若数据本身已含 \n，多余的空行会被解析器静默忽略。 */
            if (s_ipd_len < sizeof(s_ipd_buf)) {
                s_ipd_buf[s_ipd_len] = (uint8_t)'\n';
                s_ipd_len++;
            }

            /* 从 s_resp_buf 中移除整个 +IPD 包（头部+数据） */
            uint32_t remove_end = pos + data_len;
            memmove(&s_resp_buf[i], &s_resp_buf[remove_end],
                    s_resp_len - remove_end);
            s_resp_len -= (remove_end - i);
            /* 不递增 i，继续扫描当前位置（可能紧跟下一个包） */
        } else {
            i++;
        }
    }
}

/**
 * @brief  在响应缓冲中搜索子串
 * @param  keyword 要搜索的关键字
 * @retval 1 找到；0 未找到
 */
static uint8_t esp8266_resp_contains(const char *keyword)
{
    if (keyword == NULL) {
        return 0U;
    }
    /* 确保缓冲以 '\0' 结尾后用 strstr */
    if (s_resp_len < ESP8266_RESP_BUF_SIZE) {
        s_resp_buf[s_resp_len] = '\0';
    } else {
        s_resp_buf[ESP8266_RESP_BUF_SIZE - 1U] = '\0';
    }
    return (strstr((const char *)s_resp_buf, keyword) != NULL) ? 1U : 0U;
}

/**
 * @brief  等待 ESP8266 响应中出现期望关键字或失败关键字
 * @param  expect     期望关键字（如 "OK"、"CONNECT"、"SEND OK"），为 NULL 则不检查
 * @param  fail       失败关键字（如 "ERROR"、"FAIL"），为 NULL 则不检查
 * @param  timeout_ms 超时时间（毫秒）
 * @retval ESP8266_OK 命中 expect；ESP8266_ERR_RESPONSE 命中 fail；ESP8266_ERR_TIMEOUT 超时
 */
static ESP8266_Status_t esp8266_wait_response(const char *expect,
                                              const char *fail,
                                              uint32_t timeout_ms)
{
    uint32_t elapsed = 0U;

    while (elapsed < timeout_ms) {
        (void)esp8266_pump_rx();

        if ((expect != NULL) && esp8266_resp_contains(expect)) {
            return ESP8266_OK;
        }
        if ((fail != NULL) && esp8266_resp_contains(fail)) {
            return ESP8266_ERR_RESPONSE;
        }

        HAL_Delay(ESP8266_POLL_INTERVAL_MS);
        elapsed += ESP8266_POLL_INTERVAL_MS;
    }

    /* 超时前最后再泵一次 */
    (void)esp8266_pump_rx();
    if ((expect != NULL) && esp8266_resp_contains(expect)) {
        return ESP8266_OK;
    }

    /* 超时：打印原始数据帮助排查（完全无数据=接线/供电问题，有乱码=波特率问题） */
    esp8266_dump_rx("TIMEOUT");

    return ESP8266_ERR_TIMEOUT;
}

/**
 * @brief  发送原始字符串到 ESP8266（不加 \r\n）
 */
static void esp8266_send_raw(const char *str)
{
    if (str == NULL) {
        return;
    }
    BSP_UART3_Send((const uint8_t *)str, (uint32_t)strlen(str));
}

/**
 * @brief  发送 AT 指令（自动追加 \r\n），并清空接收缓冲准备收响应
 */
static void esp8266_send_cmd(const char *cmd)
{
    esp8266_flush_rx();
    esp8266_send_raw(cmd);
    esp8266_send_raw("\r\n");
}

/* ==========================================================================
 *  公开 API
 * ========================================================================== */

void BSP_ESP8266_Init(void)
{
    /* UART3 硬件已由 BSP_UART3_Init() 初始化，此处仅清空链路并等待模块上电 */
    esp8266_flush_rx();
    HAL_Delay(ESP8266_READY_DELAY_MS);   /* 等待 ESP8266 上电稳定，输出 ready */

    /* 把上电期间收到的信息打印出来（验证接收通路：正常应能看到 "ready" 等字样） */
    (void)esp8266_pump_rx();
    esp8266_dump_rx("POWER-ON");

    esp8266_flush_rx();                /* 丢弃上电时的乱码/ready 信息 */
}

ESP8266_Status_t BSP_ESP8266_TestAT(uint32_t timeout_ms)
{
    esp8266_send_cmd("AT");
    return esp8266_wait_response("OK", "ERROR", timeout_ms);
}

ESP8266_Status_t BSP_ESP8266_SetMode(ESP8266_Mode_t mode)
{
    char cmd[32];

    if ((mode != ESP8266_MODE_STA) &&
        (mode != ESP8266_MODE_AP) &&
        (mode != ESP8266_MODE_STA_AP)) {
        return ESP8266_ERR_PARAM;
    }

    (void)snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d", (int)mode);
    esp8266_send_cmd(cmd);
    return esp8266_wait_response("OK", "ERROR", ESP8266_CMD_TIMEOUT_MS);
}

ESP8266_Status_t BSP_ESP8266_JoinAP(const char *ssid, const char *password)
{
    char cmd[128];

    if ((ssid == NULL) || (password == NULL)) {
        return ESP8266_ERR_PARAM;
    }

    /* AT+CWJAP="ssid","password" —— 引号必须保留 */
    (void)snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    esp8266_send_cmd(cmd);

    /* 连接成功标志是 "WIFI GOT IP"（DHCP 完成），失败标志是 "FAIL" */
    return esp8266_wait_response("WIFI GOT IP", "FAIL", ESP8266_JOIN_TIMEOUT_MS);
}

ESP8266_Status_t BSP_ESP8266_TCPConnect(const char *ip, uint16_t port)
{
    char cmd[128];
    ESP8266_Status_t st;

    if (ip == NULL) {
        return ESP8266_ERR_PARAM;
    }

    /* 1. 设为单连接模式 */
    esp8266_send_cmd("AT+CIPMUX=0");
    st = esp8266_wait_response("OK", "ERROR", ESP8266_CMD_TIMEOUT_MS);
    if (st != ESP8266_OK) {
        return st;
    }

    /* 2. 发起 TCP 连接：AT+CIPSTART="TCP","ip",port */
    (void)snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", ip, (unsigned)port);
    esp8266_send_cmd(cmd);

    /* 成功响应包含 "CONNECT"，失败为 "ERROR" 或 "CLOSED" */
    return esp8266_wait_response("CONNECT", "ERROR", ESP8266_TCP_TIMEOUT_MS);
}

ESP8266_Status_t BSP_ESP8266_TCPSend(const uint8_t *data, uint32_t len)
{
    char cmd[32];
    ESP8266_Status_t st;

    if ((data == NULL) || (len == 0U)) {
        return ESP8266_ERR_PARAM;
    }

    /* 1. 通知模块要发送的字节数：AT+CIPSEND=len */
    (void)snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)len);
    esp8266_send_cmd(cmd);

    /* 2. 等待 ">" 提示符（模块准备好接收数据） */
    st = esp8266_wait_response(">", "ERROR", ESP8266_CMD_TIMEOUT_MS);
    if (st != ESP8266_OK) {
        return ESP8266_ERR_RESPONSE;
    }

    /* 3. 发送实际数据（不需要 \r\n，发多少就是多少） */
    BSP_UART3_Send(data, len);

    /* 4. 等待发送完成：成功 "SEND OK"，失败 "SEND FAIL" */
    return esp8266_wait_response("SEND OK", "SEND FAIL", ESP8266_SEND_TIMEOUT_MS);
}

ESP8266_Status_t BSP_ESP8266_GetIP(char *ip_buf, uint32_t buf_size)
{
    ESP8266_Status_t st;
    const char *p;
    const char *start;
    const char *end;
    uint32_t i;

    if ((ip_buf == NULL) || (buf_size == 0U)) {
        return ESP8266_ERR_PARAM;
    }

    esp8266_send_cmd("AT+CIFSR");
    st = esp8266_wait_response("OK", "ERROR", ESP8266_CIFSR_TIMEOUT_MS);
    if (st != ESP8266_OK) {
        return st;
    }

    /* 响应格式：+CIFSR:STAIP,"192.168.1.100"\r\n+CIFSR:STAMAC,"xx:xx:..."\r\nOK\r\n
       找到第一个引号后的 IP 字符串 */
    s_resp_buf[s_resp_len] = '\0';
    p = strstr((const char *)s_resp_buf, "STAIP");
    if (p == NULL) {
        return ESP8266_ERR_RESPONSE;
    }
    start = strchr(p, '"');
    if (start == NULL) {
        return ESP8266_ERR_RESPONSE;
    }
    start++;  /* 跳过左引号 */
    end = strchr(start, '"');
    if (end == NULL) {
        return ESP8266_ERR_RESPONSE;
    }

    /* 拷贝 IP 到输出缓冲 */
    for (i = 0U; (i < (buf_size - 1U)) && (start < end); i++) {
        ip_buf[i] = *start++;
    }
    ip_buf[i] = '\0';

    return ESP8266_OK;
}

ESP8266_Status_t BSP_ESP8266_Reset(void)
{
    esp8266_send_cmd("AT+RST");
    /* 复位后模块会输出乱码 + "ready"，等待 ready 表示复位完成 */
    return esp8266_wait_response("ready", "ERROR", ESP8266_JOIN_TIMEOUT_MS);
}

int8_t BSP_ESP8266_IsTCPConnected(void)
{
    ESP8266_Status_t st;

    esp8266_send_cmd("AT+CIPSTATUS");
    st = esp8266_wait_response("OK", "ERROR", ESP8266_CMD_TIMEOUT_MS);
    if (st != ESP8266_OK) {
        return -1;
    }

    /* 响应格式：STATUS:<n>\n  n=2表示已连接，n=3表示正在传输，其他为未连接 */
    s_resp_buf[s_resp_len] = '\0';
    if (strstr((const char *)s_resp_buf, "STATUS:2") != NULL) {
        return 1;
    }
    if (strstr((const char *)s_resp_buf, "STATUS:3") != NULL) {
        return 1;
    }
    return 0;
}

/**
 * @brief  非阻塞轮询 TCP 接收数据（解析 +IPD 包，提取纯数据）
 * @param  data_buf 输出缓冲区
 * @param  buf_size 缓冲区大小
 * @retval 实际提取到的数据字节数；0 表示无新数据
 * @note   内部先调用 esp8266_pump_rx() 泵入 UART3 新数据并扫描 +IPD；
 *         提取后数据从内部缓冲移除，可多次调用直至返回 0
 */
uint32_t BSP_ESP8266_PollIPD(uint8_t *data_buf, uint32_t buf_size)
{
    uint32_t len;

    if ((data_buf == NULL) || (buf_size == 0U)) {
        return 0U;
    }

    /* 泵入 UART3 新数据（内部会扫描 +IPD 并提取到 s_ipd_buf） */
    (void)esp8266_pump_rx();

    /* 取出已提取的数据 */
    len = s_ipd_len;
    if (len > buf_size) {
        len = buf_size;
    }
    if (len > 0U) {
        memcpy(data_buf, s_ipd_buf, len);
        /* 移除已取出部分，保留剩余数据 */
        memmove(s_ipd_buf, &s_ipd_buf[len], s_ipd_len - len);
        s_ipd_len -= len;
    }

    return len;
}

/* ==========================================================================
 *  MQTT 专用接口（二进制安全，不追加\n）
 * ========================================================================== */

/**
 * @brief  在 s_resp_buf 中扫描 +IPD 包，提取纯数据到 s_mqtt_buf 环形缓冲（不追加\n）
 * @note   与 esp8266_scan_ipd 的区别：不追加换行符，使用独立环形缓冲
 *
 *  注意：本项目使用 AT+CIPMUX=0 单连接模式，+IPD 格式为：
 *    +IPD,<data_len>:<data>
 *  （没有 link_id 字段；多连接模式 AT+CIPMUX=1 才是 +IPD,<link_id>,<len>:<data>）
 */
static void esp8266_scan_ipd_raw(void)
{
    uint32_t i = 0U;

    while (i < s_resp_len) {
        /* 搜索 "+IPD," 前缀 */
        if ((s_resp_buf[i] == '+') &&
            (i + 4U < s_resp_len) &&
            (s_resp_buf[i + 1U] == 'I') &&
            (s_resp_buf[i + 2U] == 'P') &&
            (s_resp_buf[i + 3U] == 'D') &&
            (s_resp_buf[i + 4U] == ',')) {

            /* 单连接模式：+IPD,<data_len>:<data>
             * pos 直接指向 data_len 的第一个数字（无需跳过 link_id） */
            uint32_t pos = i + 5U;

            /* 解析 data_len（到冒号） */
            uint32_t len_start = pos;
            while ((pos < s_resp_len) && (s_resp_buf[pos] != ':')) {
                pos++;
            }
            if (pos >= s_resp_len) {
                break;   /* 包不完整 */
            }

            uint32_t data_len = 0U;
            for (uint32_t j = len_start; j < pos; j++) {
                if ((s_resp_buf[j] >= '0') && (s_resp_buf[j] <= '9')) {
                    data_len = data_len * 10U + (uint32_t)(s_resp_buf[j] - '0');
                }
            }
            pos++;   /* 跳过冒号 */

            /* 检查数据是否完整到达 */
            if (pos + data_len > s_resp_len) {
                break;   /* 数据未到齐，等待 */
            }

            /* 提取数据到 s_mqtt_buf 环形缓冲（不追加\n，二进制安全） */
            for (uint32_t j = 0U; j < data_len; j++) {
                uint32_t next = (s_mqtt_tail + 1U) % ESP8266_MQTT_BUF_SIZE;
                if (next == s_mqtt_head) {
                    break;   /* 环形缓冲满，丢弃剩余数据 */
                }
                s_mqtt_buf[s_mqtt_tail] = s_resp_buf[pos + j];
                s_mqtt_tail = next;
            }

            /* 从 s_resp_buf 中移除整个 +IPD 包（头部+数据） */
            uint32_t remove_end = pos + data_len;
            memmove(&s_resp_buf[i], &s_resp_buf[remove_end],
                    s_resp_len - remove_end);
            s_resp_len -= (remove_end - i);
            /* 不递增 i，继续扫描当前位置（可能紧跟下一个包） */
        } else {
            i++;
        }
    }
}

void BSP_ESP8266_EnterMQTTMode(void)
{
    s_mqtt_mode = 1U;
    s_mqtt_head = 0U;
    s_mqtt_tail = 0U;
    /* 清空旧的文本模式缓冲，避免残留数据干扰 */
    s_ipd_len = 0U;
    memset(s_ipd_buf, 0, sizeof(s_ipd_buf));
}

uint32_t BSP_ESP8266_TCPRead(uint8_t *data_buf, uint32_t len, uint32_t timeout_ms)
{
    uint32_t got = 0U;
    uint32_t elapsed = 0U;

    if ((data_buf == NULL) || (len == 0U) || (s_mqtt_mode == 0U)) {
        return 0U;
    }

    while (got < len) {
        /* 先从环形缓冲取已有数据 */
        while ((got < len) && (s_mqtt_head != s_mqtt_tail)) {
            data_buf[got++] = s_mqtt_buf[s_mqtt_head];
            s_mqtt_head = (s_mqtt_head + 1U) % ESP8266_MQTT_BUF_SIZE;
        }
        if (got >= len) {
            break;
        }

        /* 泵入新数据（MQTT模式下pump_rx不会调用旧scan_ipd），然后用raw解析器提取 */
        (void)esp8266_pump_rx();
        esp8266_scan_ipd_raw();

        /* 检测连接断开 */
        if (esp8266_resp_contains("CLOSED") || esp8266_resp_contains("ERROR")) {
            break;
        }

        if (elapsed >= timeout_ms) {
            break;
        }
        HAL_Delay(ESP8266_POLL_INTERVAL_MS);
        elapsed += ESP8266_POLL_INTERVAL_MS;
    }

    return got;
}

uint8_t BSP_ESP8266_TCPIsAlive(void)
{
    int8_t st = BSP_ESP8266_IsTCPConnected();
    return (st == 1) ? 1U : 0U;
}

#endif /* BSP_ESP8266_ENABLE */
