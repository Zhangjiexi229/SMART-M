/*
 * FreeModbus Port: 从站串口驱动 (UART4 + RS485 DE=PB1)
 * 同时统一处理 UART2(主站) 和 UART4(从站) 的 HAL 回调
 */
#include "port.h"
#include "mb.h"
#include "mbport.h"

#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0
extern void MB_Master_RxCpltHandler(void);
extern void MB_Master_TxCpltHandler(void);
#endif

static UCHAR slave_rx_byte;

BOOL xMBPortSerialInit(UCHAR ucPort, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity)
{
    MB_SLAVE_RX_MODE();
    return TRUE;
}

void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
    if (xRxEnable) {
        MB_SLAVE_RX_MODE();
        HAL_UART_Receive_IT(&huart4, &slave_rx_byte, 1);
    } else {
        HAL_UART_AbortReceive_IT(&huart4);
    }
    if (xTxEnable) {
        MB_SLAVE_TX_MODE();
    }
}

void vMBPortClose(void) { }

BOOL xMBPortSerialPutByte(CHAR ucByte)
{
    uint8_t b = (uint8_t)ucByte;
    HAL_UART_Transmit(&huart4, &b, 1, 100);
    return TRUE;
}

BOOL xMBPortSerialGetByte(CHAR *pucByte)
{
    *pucByte = (CHAR)slave_rx_byte;
    return TRUE;
}

/* ===== 统一 HAL 回调分发 ===== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4) {
        /* 从站接收 */
        pxMBFrameCBByteReceived();
        HAL_UART_Receive_IT(&huart4, &slave_rx_byte, 1);
    }
#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0
    else if (huart->Instance == USART2) {
        MB_Master_RxCpltHandler();
    }
#endif
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4) {
        while (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_TC) == RESET);
        MB_SLAVE_RX_MODE();
        pxMBFrameCBTransmitterEmpty();
    }
#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0
    else if (huart->Instance == USART2) {
        MB_Master_TxCpltHandler();
    }
#endif
}
