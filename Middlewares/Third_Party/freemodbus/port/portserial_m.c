/*
 * FreeModbus Port: 主站串口驱动 (UART2 + RS485 DE=PB0)
 */
#include "port.h"
#include "mb.h"
#include "mb_m.h"
#include "mbport.h"

#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0

static UCHAR master_rx_byte;

BOOL xMBMasterPortSerialInit(UCHAR ucPort, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity)
{
    MB_MASTER_RX_MODE();
    return TRUE;
}

void vMBMasterPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
    if (xRxEnable) {
        MB_MASTER_RX_MODE();
        HAL_UART_Receive_IT(&huart2, &master_rx_byte, 1);
    } else {
        HAL_UART_AbortReceive_IT(&huart2);
    }
    if (xTxEnable) {
        MB_MASTER_TX_MODE();
    }
}

void vMBMasterPortClose(void) { }

BOOL xMBMasterPortSerialPutByte(CHAR ucByte)
{
    uint8_t b = (uint8_t)ucByte;
    HAL_UART_Transmit(&huart2, &b, 1, 100);
    return TRUE;
}

BOOL xMBMasterPortSerialGetByte(CHAR *pucByte)
{
    *pucByte = (CHAR)master_rx_byte;
    return TRUE;
}

/* 主站接收中断 — 在 HAL_UART_RxCpltCallback 中分发 */
void MB_Master_RxCpltHandler(void)
{
    pxMBMasterFrameCBByteReceived();
    HAL_UART_Receive_IT(&huart2, &master_rx_byte, 1);
}

/* 主站发送完成中断 — 在 HAL_UART_TxCpltCallback 中分发 */
void MB_Master_TxCpltHandler(void)
{
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET);
    MB_MASTER_RX_MODE();
    pxMBMasterFrameCBTransmitterEmpty();
}

#endif /* MB_MASTER_RTU_ENABLED || MB_MASTER_ASCII_ENABLED */
