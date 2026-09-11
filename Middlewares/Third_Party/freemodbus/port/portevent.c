/*
 * FreeModbus Port: 从站事件 (CMSIS-RTOS2 消息队列)
 */
#include "port.h"
#include "mb.h"
#include "mbport.h"

BOOL xMBPortEventInit(void)
{
    /* 队列在 app_modbus.c 中创建，此处仅确认 */
    return TRUE;
}

BOOL xMBPortEventPost(eMBEventType eEvent)
{
    uint32_t event = (uint32_t)eEvent;
    if (mbSlaveEventQueueHandle != NULL) {
        osMessageQueuePut(mbSlaveEventQueueHandle, &event, 0, 0);
    }
    return TRUE;
}

BOOL xMBPortEventGet(eMBEventType *eEvent)
{
    uint32_t event;
    if (osMessageQueueGet(mbSlaveEventQueueHandle, &event, NULL, osWaitForever) == osOK) {
        *eEvent = (eMBEventType)event;
        return TRUE;
    }
    return FALSE;
}
