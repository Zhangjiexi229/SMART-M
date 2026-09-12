/**
 ******************************************************************************
 * @file    app_modbus.h
 * @brief   FreeModbus 主从一体应用层头文件
 *
 *  硬件分配：
 *    从站(Slave):  UART4 (PC10=TX, PC11=RX) + TIM7 + DE=PB1
 *    主站(Master): UART2 (PA2=TX,  PA3=RX)  + TIM6 + DE=PB0
 *
 *  从站寄存器映射：
 *    输入寄存器(04): 30001=温度×10, 30002=湿度×10
 *    保持寄存器(03/06/16): 40001=温度×10, 40002=湿度×10,
 *                          40003=LED状态(bit0~3), 40004=MQTT连接状态
 *    线圈(01/05/15): 00001~00004 = LED1~LED4
 *
 *  主站功能：周期读取外部 Modbus 从站设备，数据存入共享快照供 MQTT 上报
 ******************************************************************************
 */
#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>
#include "cmsis_os.h"

/* ===== OS 对象（在 app_modbus.c 中定义） ===== */
extern osMessageQueueId_t mbSlaveEventQueueHandle;
extern osMessageQueueId_t mbMasterEventQueueHandle;
extern osSemaphoreId_t    mbMasterRunSemHandle;

/* ===== 主站采集数据快照（主站写，MQTT读） ===== */
#define MB_MASTER_MAX_REGISTERS  16
typedef struct {
    uint8_t  slave_addr;                          /* 从站地址 */
    uint8_t  valid;                               /* 数据有效标志 */
    uint16_t regs[MB_MASTER_MAX_REGISTERS];       /* 采集到的寄存器 */
    uint8_t  reg_count;                           /* 实际寄存器数量 */
} ModbusMaster_Snapshot_t;

/**
 * @brief  Modbus 从站任务入口
 */
void APP_ModbusSlave_Task(void *argument);

/**
 * @brief  Modbus 主站任务入口
 */
void APP_ModbusMaster_Task(void *argument);

/**
 * @brief  获取主站采集快照（线程安全）
 */
void APP_ModbusMaster_GetSnapshot(ModbusMaster_Snapshot_t *out);

/**
 * @brief  初始化 RS485 DE 引脚 GPIO（在 APP_Init 中调用）
 */
void APP_Modbus_RS485_GPIO_Init(void);

#endif /* APP_MODBUS_H */
