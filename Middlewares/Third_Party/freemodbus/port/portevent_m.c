/*
 * FreeModbus Port: 主站事件 (CMSIS-RTOS2 消息队列 + 信号量)
 */
#include "port.h"
#include "mb.h"
#include "mb_m.h"
#include "mbport.h"

#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0

BOOL xMBMasterPortEventInit(void)
{
    return TRUE;
}

BOOL xMBMasterPortEventPost(eMBMasterEventType eEvent)
{
    uint32_t event = (uint32_t)eEvent;
    if (mbMasterEventQueueHandle != NULL) {
        osMessageQueuePut(mbMasterEventQueueHandle, &event, 0, 0);
    }
    return TRUE;
}

BOOL xMBMasterPortEventGet(eMBMasterEventType *eEvent)
{
    uint32_t event;
    if (osMessageQueueGet(mbMasterEventQueueHandle, &event, NULL, osWaitForever) == osOK) {
        *eEvent = (eMBMasterEventType)event;
        return TRUE;
    }
    return FALSE;
}

/* ===== 主站运行资源（信号量互斥） ===== */
void vMBMasterOsResInit(void)
{
    /* 信号量在 app_modbus.c 中创建，初始计数=1 */
}

BOOL xMBMasterRunResTake(int32_t time)
{
    if (mbMasterRunSemHandle != NULL) {
        uint32_t ticks = (time < 0) ? osWaitForever : (uint32_t)time;
        return (osSemaphoreAcquire(mbMasterRunSemHandle, ticks) == osOK) ? TRUE : FALSE;
    }
    return TRUE;
}

void vMBMasterRunResRelease(void)
{
    if (mbMasterRunSemHandle != NULL) {
        osSemaphoreRelease(mbMasterRunSemHandle);
    }
}

/* ===== 主站错误/成功回调 ===== */
void vMBMasterErrorCBRespondTimeout(UCHAR ucDestAddress, const UCHAR *pucPDUData, USHORT ucPDULength)
{
    (void)ucDestAddress; (void)pucPDUData; (void)ucPDULength;
    xMBMasterPortEventPost(EV_MASTER_ERROR_RESPOND_TIMEOUT);
}

void vMBMasterErrorCBReceiveData(UCHAR ucDestAddress, const UCHAR *pucPDUData, USHORT ucPDULength)
{
    (void)ucDestAddress; (void)pucPDUData; (void)ucPDULength;
    xMBMasterPortEventPost(EV_MASTER_ERROR_RECEIVE_DATA);
}

void vMBMasterErrorCBExecuteFunction(UCHAR ucDestAddress, const UCHAR *pucPDUData, USHORT ucPDULength)
{
    (void)ucDestAddress; (void)pucPDUData; (void)ucPDULength;
    xMBMasterPortEventPost(EV_MASTER_ERROR_EXECUTE_FUNCTION);
}

void vMBMasterCBRequestScuuess(void)
{
    xMBMasterPortEventPost(EV_MASTER_PROCESS_SUCESS);
}

/* 等待主站请求完成，返回结果 */
eMBMasterReqErrCode eMBMasterWaitRequestFinish(void)
{
    eMBMasterReqErrCode eErrStatus = MB_MRE_NO_ERR;
    uint32_t event;
    /* 等待成功或错误事件 */
    if (osMessageQueueGet(mbMasterEventQueueHandle, &event, NULL, osWaitForever) == osOK) {
        switch (event) {
        case EV_MASTER_PROCESS_SUCESS:
            break;
        case EV_MASTER_ERROR_RESPOND_TIMEOUT:
            eErrStatus = MB_MRE_TIMEDOUT;
            break;
        case EV_MASTER_ERROR_RECEIVE_DATA:
            eErrStatus = MB_MRE_REV_DATA;
            break;
        case EV_MASTER_ERROR_EXECUTE_FUNCTION:
            eErrStatus = MB_MRE_EXE_FUN;
            break;
        default:
            break;
        }
    }
    return eErrStatus;
}

#endif /* MB_MASTER_RTU_ENABLED || MB_MASTER_ASCII_ENABLED */
