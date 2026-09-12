/**
 ******************************************************************************
 * @file    common.h
 * @brief   公共类型定义、状态码枚举 — 所有层共享的基础类型
 ******************************************************************************
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

/**
 * @brief  通用状态码 — 所有BSP/APP函数的统一返回值
 */
typedef enum {
    COMMON_OK = 0,
    COMMON_ERROR,
    COMMON_TIMEOUT,
    COMMON_BUSY,
    COMMON_INVALID_PARAM
} common_status_t;

#endif /* COMMON_H */
