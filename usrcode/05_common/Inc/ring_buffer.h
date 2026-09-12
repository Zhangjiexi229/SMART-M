/**
 ******************************************************************************
 * @file    ring_buffer.h
 * @brief   通用环形缓冲（Ring Buffer）— 单生产者/单消费者模型
 *
 *  设计说明：
 *    - 无锁 SPSC（Single Producer / Single Consumer）实现：
 *        head（写索引）仅由生产者修改，tail（读索引）仅由消费者修改，
 *        在 Cortex-M 上对 uint32 索引的读写为原子操作，故无需关中断。
 *    - 典型使用场景：ISR（生产者）写入 / 任务（消费者）读取，
 *      实现中断上下文与任务上下文之间的数据解耦缓冲。
 *    - 预留 1 个字节空间以区分"满"与"空"，最大可用容量 = size - 1。
 ******************************************************************************
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>

/** @brief 环形缓冲结构体 */
typedef struct {
    uint8_t          *buffer;   /*!< 数据缓冲区（由外部提供） */
    uint32_t          size;     /*!< 缓冲区字节数（2的幂最优，非强制） */
    volatile uint32_t head;     /*!< 写索引（生产者更新） */
    volatile uint32_t tail;     /*!< 读索引（消费者更新） */
} ring_buffer_t;

/**
 * @brief  初始化环形缓冲
 * @param  rb   环形缓冲对象
 * @param  buf  数据缓冲区
 * @param  size 缓冲区字节数
 */
void RB_Init(ring_buffer_t *rb, uint8_t *buf, uint32_t size);

/**
 * @brief  复位环形缓冲（清空，索引归零）
 * @note   仅在确定无并发读写时调用（如错误恢复、重新初始化）
 * @param  rb   环形缓冲对象
 */
void RB_Reset(ring_buffer_t *rb);

/**
 * @brief  查询已缓冲（可读）字节数
 * @param  rb   环形缓冲对象
 * @return 可读字节数
 */
uint32_t RB_Used(ring_buffer_t *rb);

/**
 * @brief  查询空闲（可写）字节数
 * @param  rb   环形缓冲对象
 * @return 可写字节数
 */
uint32_t RB_Free(ring_buffer_t *rb);

/**
 * @brief  写入数据（仅生产者调用，可安全用于ISR）
 * @param  rb   环形缓冲对象
 * @param  data 源数据指针
 * @param  len  请求写入字节数
 * @return 实际写入字节数（缓冲满时小于 len）
 */
uint32_t RB_Write(ring_buffer_t *rb, const uint8_t *data, uint32_t len);

/**
 * @brief  读取数据（仅消费者调用）
 * @param  rb   环形缓冲对象
 * @param  data 目的数据指针
 * @param  len  请求读取字节数
 * @return 实际读取字节数（缓冲空时小于 len）
 */
uint32_t RB_Read(ring_buffer_t *rb, uint8_t *data, uint32_t len);

/**
 * @brief  预读数据（不移动读索引）
 * @param  rb   环形缓冲对象
 * @param  data 目的数据指针
 * @param  len  请求读取字节数
 * @return 实际可读字节数
 */
uint32_t RB_Peek(ring_buffer_t *rb, uint8_t *data, uint32_t len);

#endif /* RING_BUFFER_H */
