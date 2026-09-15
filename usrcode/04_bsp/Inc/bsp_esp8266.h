/**
 ******************************************************************************
 * @file    bsp_esp8266.h
 * @brief   ESP8266 WiFi 模块 AT 指令驱动头文件
 *
 *  依赖：BSP_UART3（USART3，PB10=TX，PB11=RX，115200 8N1）
 *
 *  数据通路：
 *    APP任务 -> BSP_ESP8266_xxx() -> BSP_UART3_Send() -> ESP8266 RX
 *    ESP8266 TX -> BSP_UART3 DMA+IDLE -> 软件环形缓冲 -> BSP_ESP8266 轮询解析
 *
 *  使用说明：
 *    1. 本驱动所有函数均为阻塞式，在任务上下文中调用（内部用 osDelay 让出 CPU）
 *    2. 不要在 ISR 中调用
 *    3. 典型初始化序列：
 *         BSP_ESP8266_Init()
 *         BSP_ESP8266_TestAT()
 *         BSP_ESP8266_SetMode(ESP8266_MODE_STA)
 *         BSP_ESP8266_JoinAP(ssid, pwd)
 *         BSP_ESP8266_TCPConnect(ip, port)
 *         BSP_ESP8266_TCPSend(data, len)   // 周期调用
 ******************************************************************************
 */
#ifndef BSP_ESP8266_H
#define BSP_ESP8266_H

#include <stdint.h>
/* ========== ESP8266 工作模式 ========== */
typedef enum {
    ESP8266_MODE_STA    = 1,   /*!< Station 模式：连接路由器 */
    ESP8266_MODE_AP     = 2,   /*!< AP 模式：自身作为热点 */
    ESP8266_MODE_STA_AP = 3    /*!< Station+AP 混合模式 */
} ESP8266_Mode_t;

/* ========== 驱动状态码 ========== */
typedef enum {
    ESP8266_OK = 0,            /*!< 操作成功，收到期望响应 */
    ESP8266_ERR_TIMEOUT,       /*!< 等待响应超时 */
    ESP8266_ERR_RESPONSE,      /*!< 收到 ERROR 或意外响应 */
    ESP8266_ERR_NOT_READY,     /*!< 模块未就绪（AT 测试失败） */
    ESP8266_ERR_PARAM          /*!< 参数错误 */
} ESP8266_Status_t;

/* ==================== API ==================== */

/**
 * @brief  初始化 ESP8266 驱动
 * @note   清空 UART3 接收缓冲，等待模块上电稳定（约 2 秒）
 *         UART3 硬件初始化已由 BSP_UART3_Init() 完成，此处不重复
 */
void BSP_ESP8266_Init(void);

/**
 * @brief  发送 AT 测试指令，确认模块在线
 * @param  timeout_ms 超时时间（毫秒）
 * @retval ESP8266_OK 收到 OK；ESP8266_ERR_TIMEOUT 超时
 */
ESP8266_Status_t BSP_ESP8266_TestAT(uint32_t timeout_ms);

/**
 * @brief  设置 WiFi 模式（AT+CWMODE）
 * @param  mode 工作模式
 * @retval ESP8266_OK 成功；其他失败
 */
ESP8266_Status_t BSP_ESP8266_SetMode(ESP8266_Mode_t mode);

/**
 * @brief  连接 WiFi 路由器（AT+CWJAP）
 * @param  ssid     WiFi 名称（以'\0'结尾，最长 32 字符）
 * @param  password WiFi 密码（以'\0'结尾，最长 64 字符）
 * @retval ESP8266_OK 连接成功（收到 WIFI GOT IP）；其他失败
 * @note   路由器必须为 2.4GHz，ESP8266 不支持 5GHz
 */
ESP8266_Status_t BSP_ESP8266_JoinAP(const char *ssid, const char *password);

/**
 * @brief  建立 TCP 连接（AT+CIPMUX=0 + AT+CIPSTART）
 * @param  ip   服务器 IP 地址字符串（如 "192.168.1.100"）
 * @param  port 服务器端口号
 * @retval ESP8266_OK 连接成功（收到 CONNECT）；其他失败
 */
ESP8266_Status_t BSP_ESP8266_TCPConnect(const char *ip, uint16_t port);

/**
 * @brief  通过已建立的 TCP 连接发送数据（AT+CIPSEND）
 * @param  data 待发送数据
 * @param  len  数据长度（字节）
 * @retval ESP8266_OK 发送成功（收到 SEND OK）；其他失败
 * @note   发送前必须已通过 BSP_ESP8266_TCPConnect() 建立连接
 */
ESP8266_Status_t BSP_ESP8266_TCPSend(const uint8_t *data, uint32_t len);

/**
 * @brief  查询模块当前 IP 地址（AT+CIFSR）
 * @param  ip_buf   输出缓冲区，存放点分十进制 IP 字符串
 * @param  buf_size 缓冲区大小
 * @retval ESP8266_OK 成功；其他失败
 */
ESP8266_Status_t BSP_ESP8266_GetIP(char *ip_buf, uint32_t buf_size);

/**
 * @brief  软件复位 ESP8266（AT+RST）
 * @retval ESP8266_OK 复位成功（收到 ready）；其他失败
 */
ESP8266_Status_t BSP_ESP8266_Reset(void);

/**
 * @brief  查询 TCP 连接状态（AT+CIPSTATUS）
 * @retval 1 已连接；0 未连接；负数表示查询失败
 */
int8_t BSP_ESP8266_IsTCPConnected(void);

/**
 * @brief  关闭当前 TCP 连接（AT+CIPCLOSE）
 * @retval ESP8266_OK 关闭成功；其他失败
 * @note   用于 MQTT 重连前清理半死连接，避免 CIPSTART 报 ALREADY CONNECT
 */
ESP8266_Status_t BSP_ESP8266_TCPClose(void);
uint8_t           BSP_ESP8266_IsTCPClosed(void);   /* 纯内存查询，不发AT */
void              BSP_ESP8266_ClearClosedFlag(void);
int8_t            BSP_ESP8266_IsWiFiConnected(void);      /* 1=online, 0=lost(STATUS:5), -1=query failed */
uint8_t           BSP_ESP8266_IsWiFiClosed(void);         /* async "WIFI DISCONNECT" event received */
void              BSP_ESP8266_ClearWiFiClosedFlag(void);  /* clear async WiFi-lost flag */

/**
 * @brief  非阻塞轮询 TCP 接收数据（解析 ESP8266 的 +IPD 包）
 * @param  data_buf 输出缓冲区，存放提取出的纯数据（不含 +IPD,<id>,<len>: 头）
 * @param  buf_size 缓冲区大小
 * @retval 实际提取到的数据字节数；0 表示无新数据
 * @note   应在任务主循环中频繁调用（建议每 100~500ms 一次）；
 *         提取后数据从内部缓冲移除，可多次调用直至返回 0
 * @note   本接口会在每段数据后自动追加 '\n'，仅适用于文本指令场景；
 *         MQTT 二进制场景请使用 BSP_ESP8266_TCPRead()
 */
uint32_t BSP_ESP8266_PollIPD(uint8_t *data_buf, uint32_t buf_size);

/* ==================== MQTT 专用接口（二进制安全，不追加\n） ==================== */

/**
 * @brief  进入 MQTT 模式：切换 +IPD 解析器到原始数据通道（不追加\n）
 * @note   调用后 BSP_ESP8266_PollIPD() 将不再返回数据（二者互斥）；
 *         必须在建立 TCP 连接之后、首次 MQTT 通信之前调用
 */
void BSP_ESP8266_EnterMQTTMode(void);

/**
 * @brief  MQTT专用：阻塞读取TCP原始数据（二进制安全，不追加\n）
 * @param  data_buf   输出缓冲区
 * @param  len        期望读取的字节数
 * @param  timeout_ms 超时（毫秒）
 * @retval 实际读到的字节数（可能小于len）；连接断开返回0
 * @note   必须先调用 BSP_ESP8266_EnterMQTTMode()；
 *         与 BSP_ESP8266_PollIPD() 互斥，不能同时使用
 */
uint32_t BSP_ESP8266_TCPRead(uint8_t *data_buf, uint32_t len, uint32_t timeout_ms);

/**
 * @brief  MQTT专用：查询TCP连接是否仍然存活
 * @retval 1=连接正常，0=已断开或查询失败
 */
uint8_t BSP_ESP8266_TCPIsAlive(void);

#endif /* BSP_ESP8266_H */
