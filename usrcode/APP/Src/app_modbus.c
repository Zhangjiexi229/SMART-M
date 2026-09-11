/**
 ******************************************************************************
 * @file    app_modbus.c
 * @brief   FreeModbus 主从一体应用层实现
 *
 *  从站：被外部主站(PLC/上位机)读取温湿度、控制LED
 *  主站：主动读取外部Modbus从站设备，数据供MQTT上报
 ******************************************************************************
 */
#include "app_modbus.h"
#include "mb.h"
#include "mb_m.h"
#include "mbport.h"
#include "port.h"
#include "app_dht11.h"
#include "bsp_led.h"
#include "module_cfg.h"
#if APP_MQTT_ENABLE
#include "app_mqtt.h"
#endif
#include <string.h>
#include <stdio.h>

#if APP_MODBUS_ENABLE

/* ==========================================================================
 *  硬件句柄定义（如已在CubeMX中配置UART4/TIM7/TIM12，请删除此处定义）
 * ========================================================================== */
UART_HandleTypeDef huart4;
TIM_HandleTypeDef  htim7;
TIM_HandleTypeDef  htim12;

/* ==========================================================================
 *  OS 对象定义
 * ========================================================================== */
osMessageQueueId_t mbSlaveEventQueueHandle  = NULL;
osMessageQueueId_t mbMasterEventQueueHandle = NULL;
osSemaphoreId_t    mbMasterRunSemHandle     = NULL;

/* ==========================================================================
 *  主站采集快照
 * ========================================================================== */
static ModbusMaster_Snapshot_t s_master_snapshot = {0};
static osMutexId_t s_master_mutex = NULL;

/* 主站配置（可按需修改） */
#define MB_MASTER_TARGET_ADDR     2U      /* 外部从站地址 */
#define MB_MASTER_REG_START       0U      /* 起始寄存器(0=40001) */
#define MB_MASTER_REG_COUNT       4U      /* 读取寄存器数量 */
#define MB_MASTER_POLL_PERIOD_MS  5000U   /* 采集周期 */

/* ==========================================================================
 *  RS485 DE 引脚 GPIO 初始化
 * ========================================================================== */
void APP_Modbus_RS485_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB0 = 主站 DE/RE, PB1 = 从站 DE/RE */
    GPIO_InitStruct.Pin   = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* 默认接收模式 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
}

/* ==========================================================================
 *  Modbus 硬件初始化（UART4从站 + UART2主站改波特率 + TIM6/TIM7）
 *  注：如已在CubeMX中配置这些外设，可删除此函数，仅保留GPIO初始化
 * ========================================================================== */
static void APP_Modbus_HW_Init(void)
{
    static uint8_t s_hw_inited = 0;
    if (s_hw_inited) return;
    s_hw_inited = 1;

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* ---- UART4 时钟 + GPIO (PC10=TX, PC11=RX) ---- */
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    huart4.Instance          = UART4;
    huart4.Init.BaudRate     = 9600;
    huart4.Init.WordLength   = UART_WORDLENGTH_8B;
    huart4.Init.StopBits     = UART_STOPBITS_1;
    huart4.Init.Parity       = UART_PARITY_NONE;
    huart4.Init.Mode         = UART_MODE_TX_RX;
    huart4.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart4);

    /* UART4 中断（优先级5，FreeRTOS安全阈值） */
    HAL_NVIC_SetPriority(UART4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);

    /* ---- UART2 改波特率为9600（CubeMX默认115200） ---- */
    huart2.Init.BaudRate = 9600;
    HAL_UART_Init(&huart2);
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    /* ---- TIM12 主站定时器 (0.1ms/tick) ---- */
    __HAL_RCC_TIM12_CLK_ENABLE();
    htim12.Instance               = TIM12;
    htim12.Init.Prescaler         = 8399;   /* 84MHz/8400 = 10kHz = 0.1ms */
    htim12.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim12.Init.Period            = 65535;
    htim12.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim12);
    HAL_NVIC_SetPriority(TIM8_BRK_TIM12_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM8_BRK_TIM12_IRQn);

    /* ---- TIM7 从站定时器 (0.1ms/tick) ---- */
    __HAL_RCC_TIM7_CLK_ENABLE();
    htim7.Instance               = TIM7;
    htim7.Init.Prescaler         = 8399;
    htim7.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim7.Init.Period            = 65535;
    htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim7);
    HAL_NVIC_SetPriority(TIM7_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
}

/* ==========================================================================
 *  中断处理函数（如CubeMX已生成，请删除此处，在CubeMX生成的it.c中调用HAL_IRQHandler）
 * ========================================================================== */
void UART4_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart4);
}

void TIM8_BRK_TIM12_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim12);
}

void TIM7_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim7);
}

/* ==========================================================================
 *  从站寄存器映射回调
 * ========================================================================== */

/* 输入寄存器(04)：只读，30001=温度×10, 30002=湿度×10 */
eMBErrorCode eMBRegInputCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNRegs)
{
    if (usAddress < 1 || usAddress + usNRegs > 3)
        return MB_ENOREG;

    DHT11_Snapshot_t snap;
    APP_DHT11_GetSnapshot(&snap);
    uint16_t input_regs[2] = {
        (uint16_t)(snap.temperature_int * 10 + snap.temperature_dec),
        (uint16_t)(snap.humidity_int * 10 + snap.humidity_dec)
    };

    for (int i = 0; i < usNRegs; i++) {
        uint16_t val = input_regs[usAddress - 1 + i];
        pucRegBuffer[i * 2]     = (UCHAR)(val >> 8);
        pucRegBuffer[i * 2 + 1] = (UCHAR)(val & 0xFF);
    }
    return MB_ENOERR;
}

/* 保持寄存器(03/06/16)：可读可写
 * 40001=温度×10, 40002=湿度×10, 40003=LED状态, 40004=MQTT连接状态 */
eMBErrorCode eMBRegHoldingCB(UCHAR *pucRegBuffer, USHORT usAddress,
                              USHORT usNRegs, eMBRegisterMode eMode)
{
    if (usAddress < 1 || usAddress + usNRegs > 5)
        return MB_ENOREG;

    static uint16_t holding_regs[4] = {0};

    if (eMode == MB_REG_READ) {
        DHT11_Snapshot_t snap;
        APP_DHT11_GetSnapshot(&snap);
        holding_regs[0] = (uint16_t)(snap.temperature_int * 10 + snap.temperature_dec);
        holding_regs[1] = (uint16_t)(snap.humidity_int * 10 + snap.humidity_dec);
        holding_regs[2] = (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_14) ? 0 : 1)
                        | (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_13) ? 0 : 2)
                        | (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6)  ? 0 : 4)
                        | (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_11) ? 0 : 8);
#if APP_MQTT_ENABLE
        holding_regs[3] = APP_MQTT_GetStatus();
#else
        holding_regs[3] = 0;
#endif

        for (int i = 0; i < usNRegs; i++) {
            uint16_t val = holding_regs[usAddress - 1 + i];
            pucRegBuffer[i * 2]     = (UCHAR)(val >> 8);
            pucRegBuffer[i * 2 + 1] = (UCHAR)(val & 0xFF);
        }
    } else {  /* MB_REG_WRITE */
        for (int i = 0; i < usNRegs; i++) {
            holding_regs[usAddress - 1 + i] =
                ((uint16_t)pucRegBuffer[i * 2] << 8) | pucRegBuffer[i * 2 + 1];
        }
        /* 写 40003 时更新 LED */
        if (usAddress <= 3 && usAddress + usNRegs > 3) {
            uint16_t led_mask = holding_regs[2];
            if (led_mask & 0x01) BSP_LED_On(BSP_LED1);  else BSP_LED_Off(BSP_LED1);
            if (led_mask & 0x02) BSP_LED_On(BSP_LED2);  else BSP_LED_Off(BSP_LED2);
            if (led_mask & 0x04) BSP_LED_On(BSP_LED3);  else BSP_LED_Off(BSP_LED3);
            if (led_mask & 0x08) BSP_LED_On(BSP_LED4);  else BSP_LED_Off(BSP_LED4);
        }
    }
    return MB_ENOERR;
}

/* 线圈(01/05/15)：00001~00004 = LED1~LED4 */
eMBErrorCode eMBRegCoilsCB(UCHAR *pucRegBuffer, USHORT usAddress,
                            USHORT usNCoils, eMBRegisterMode eMode)
{
    if (usAddress < 1 || usAddress + usNCoils > 5)
        return MB_ENOREG;

    if (eMode == MB_REG_READ) {
        uint8_t mask = 0;
        mask |= HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_14) ? 0 : 1;
        mask |= HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_13) ? 0 : 2;
        mask |= HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6)  ? 0 : 4;
        mask |= HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_11) ? 0 : 8;
        pucRegBuffer[0] = mask;
    } else {
        uint8_t mask = pucRegBuffer[0];
        if (mask & 0x01) BSP_LED_On(BSP_LED1);  else BSP_LED_Off(BSP_LED1);
        if (mask & 0x02) BSP_LED_On(BSP_LED2);  else BSP_LED_Off(BSP_LED2);
        if (mask & 0x04) BSP_LED_On(BSP_LED3);  else BSP_LED_Off(BSP_LED3);
        if (mask & 0x08) BSP_LED_On(BSP_LED4);  else BSP_LED_Off(BSP_LED4);
    }
    return MB_ENOERR;
}

/* 离散输入(02)：本项目不使用 */
eMBErrorCode eMBRegDiscreteCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNDiscrete)
{
    (void)pucRegBuffer; (void)usAddress; (void)usNDiscrete;
    return MB_ENOREG;
}

/* ==========================================================================
 *  主机寄存器回调（收到从站响应时被调用）
 * ========================================================================== */
#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0

/* 主机读保持寄存器响应回调：把数据存入快照 */
eMBErrorCode eMBMasterRegHoldingCB(UCHAR *pucRegBuffer, USHORT usAddress,
                                    USHORT usNRegs, eMBRegisterMode eMode)
{
    if (eMode == MB_REG_READ) {
        if (s_master_mutex != NULL) {
            osMutexAcquire(s_master_mutex, osWaitForever);
            s_master_snapshot.slave_addr = MB_MASTER_TARGET_ADDR;
            s_master_snapshot.valid = 1;
            s_master_snapshot.reg_count = usNRegs;
            for (int i = 0; i < usNRegs && i < MB_MASTER_MAX_REGISTERS; i++) {
                s_master_snapshot.regs[usAddress + i] =
                    ((uint16_t)pucRegBuffer[i * 2] << 8) | pucRegBuffer[i * 2 + 1];
            }
            osMutexRelease(s_master_mutex);
        }
    }
    return MB_ENOERR;
}

eMBErrorCode eMBMasterRegInputCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNRegs)
{
    (void)pucRegBuffer; (void)usAddress; (void)usNRegs;
    return MB_ENOERR;
}

eMBErrorCode eMBMasterRegCoilsCB(UCHAR *pucRegBuffer, USHORT usAddress,
                                  USHORT usNCoils, eMBRegisterMode eMode)
{
    (void)pucRegBuffer; (void)usAddress; (void)usNCoils; (void)eMode;
    return MB_ENOERR;
}

eMBErrorCode eMBMasterRegDiscreteCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNDiscrete)
{
    (void)pucRegBuffer; (void)usAddress; (void)usNDiscrete;
    return MB_ENOERR;
}

#endif /* MB_MASTER_RTU_ENABLED || MB_MASTER_ASCII_ENABLED */

/* ==========================================================================
 *  从站任务
 * ========================================================================== */
void APP_ModbusSlave_Task(void *argument)
{
    (void)argument;

    /* 硬件初始化（UART4+TIM7+UART2改波特率+TIM6） */
    APP_Modbus_HW_Init();

    /* 创建从站事件队列 */
    if (mbSlaveEventQueueHandle == NULL) {
        mbSlaveEventQueueHandle = osMessageQueueNew(8, sizeof(uint32_t), NULL);
    }

    /* 初始化 Modbus 从站：地址=1, RTU模式, 端口=0, 9600, 无校验 */
    eMBErrorCode eStatus = eMBInit(MB_RTU, 0x01, 0, 9600, MB_PAR_NONE);
    if (eStatus != MB_ENOERR) {
        printf("[Modbus-Slave] Init failed: %d\r\n", eStatus);
        osThreadExit();
    }

    eStatus = eMBEnable();
    if (eStatus != MB_ENOERR) {
        printf("[Modbus-Slave] Enable failed: %d\r\n", eStatus);
        osThreadExit();
    }

    printf("[Modbus-Slave] Started: addr=1, UART4, 9600 8N1\r\n");

    for (;;) {
        eMBPoll();  /* 内部阻塞等待事件 */
    }
}

/* ==========================================================================
 *  主站任务
 * ========================================================================== */
void APP_ModbusMaster_Task(void *argument)
{
    (void)argument;

    /* 硬件初始化（幂等，从站任务若已初始化则直接返回） */
    APP_Modbus_HW_Init();

    /* 创建主站事件队列和信号量 */
    if (mbMasterEventQueueHandle == NULL) {
        mbMasterEventQueueHandle = osMessageQueueNew(8, sizeof(uint32_t), NULL);
    }
    if (mbMasterRunSemHandle == NULL) {
        mbMasterRunSemHandle = osSemaphoreNew(1, 1, NULL);
    }
    if (s_master_mutex == NULL) {
        s_master_mutex = osMutexNew(NULL);
    }

    vMBMasterOsResInit();

    /* 初始化 Modbus 主站：RTU模式, 端口=0, 9600, 无校验 */
    eMBErrorCode eStatus = eMBMasterInit(MB_RTU, 0, 9600, MB_PAR_NONE);
    if (eStatus != MB_ENOERR) {
        printf("[Modbus-Master] Init failed: %d\r\n", eStatus);
        osThreadExit();
    }

    eStatus = eMBMasterEnable();
    if (eStatus != MB_ENOERR) {
        printf("[Modbus-Master] Enable failed: %d\r\n", eStatus);
        osThreadExit();
    }

    printf("[Modbus-Master] Started: UART2, 9600 8N1, polling addr=%u\r\n", MB_MASTER_TARGET_ADDR);

    for (;;) {
        /* 读取外部从站的保持寄存器（数据通过 eMBMasterRegHoldingCB 回调存入快照） */
        eMBMasterReqErrCode err = eMBMasterReqReadHoldingRegister(
            MB_MASTER_TARGET_ADDR,
            MB_MASTER_REG_START,
            MB_MASTER_REG_COUNT,
            1000  /* 超时ms */
        );

        if (err == MB_MRE_NO_ERR) {
            printf("[Modbus-Master] Read addr=%u OK\r\n", MB_MASTER_TARGET_ADDR);
        } else {
            printf("[Modbus-Master] Read addr=%u FAILED: err=%d\r\n", MB_MASTER_TARGET_ADDR, err);
        }

        osDelay(MB_MASTER_POLL_PERIOD_MS);
    }
}

/* ==========================================================================
 *  主站快照读取
 * ========================================================================== */
void APP_ModbusMaster_GetSnapshot(ModbusMaster_Snapshot_t *out)
{
    if (out == NULL) return;
    if (s_master_mutex != NULL) {
        osMutexAcquire(s_master_mutex, osWaitForever);
        memcpy(out, &s_master_snapshot, sizeof(ModbusMaster_Snapshot_t));
        osMutexRelease(s_master_mutex);
    } else {
        memset(out, 0, sizeof(ModbusMaster_Snapshot_t));
    }
}

#endif /* APP_MODBUS_ENABLE */
