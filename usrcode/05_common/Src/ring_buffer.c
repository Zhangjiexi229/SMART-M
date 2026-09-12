/**
 ******************************************************************************
 * @file    ring_buffer.c
 * @brief   通用环形缓冲实现 — 无锁SPSC模型
 ******************************************************************************
 */
#include "ring_buffer.h"

void RB_Init(ring_buffer_t *rb, uint8_t *buf, uint32_t size)
{
    if ((rb == NULL) || (buf == NULL) || (size == 0U)) {
        return;
    }
    rb->buffer = buf;
    rb->size   = size;
    rb->head   = 0U;
    rb->tail   = 0U;
}

void RB_Reset(ring_buffer_t *rb)
{
    if (rb == NULL) {
        return;
    }
    rb->head = 0U;
    rb->tail = 0U;
}

uint32_t RB_Used(ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0U;
    }
    return (rb->size + rb->head - rb->tail) % rb->size;
}

uint32_t RB_Free(ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0U;
    }
    /* 预留1字节用于区分满/空 */
    return rb->size - 1U - RB_Used(rb);
}

uint32_t RB_Write(ring_buffer_t *rb, const uint8_t *data, uint32_t len)
{
    uint32_t free;
    uint32_t i;

    if ((rb == NULL) || (data == NULL)) {
        return 0U;
    }
    free = RB_Free(rb);
    if (len > free) {
        len = free;   /* 空间不足时只写入可容纳的部分 */
    }
    for (i = 0U; i < len; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1U) % rb->size;
    }
    return len;
}

uint32_t RB_Read(ring_buffer_t *rb, uint8_t *data, uint32_t len)
{
    uint32_t used;
    uint32_t i;

    if ((rb == NULL) || (data == NULL)) {
        return 0U;
    }
    used = RB_Used(rb);
    if (len > used) {
        len = used;   /* 数据不足时只读取可用部分 */
    }
    for (i = 0U; i < len; i++) {
        data[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1U) % rb->size;
    }
    return len;
}

uint32_t RB_Peek(ring_buffer_t *rb, uint8_t *data, uint32_t len)
{
    uint32_t used;
    uint32_t i;
    uint32_t idx;

    if ((rb == NULL) || (data == NULL)) {
        return 0U;
    }
    used = RB_Used(rb);
    if (len > used) {
        len = used;
    }
    idx = rb->tail;
    for (i = 0U; i < len; i++) {
        data[i] = rb->buffer[idx];
        idx = (idx + 1U) % rb->size;
    }
    return len;
}
