/**
 ******************************************************************************
 * @file    bsp_key_matrix.c
 * @brief   4x4矩阵键盘驱动实现 — GPIO行扫描+列读取
 *
 *  引脚分配（GPIOE，避开 PE3/PE4 板载按键与 PE5(HC-SR04 预留)）：
 *    行 R1-R4: PE2 / PE6 / PE7 / PE10   （输出推挽，扫描时拉低当前行）
 *    列 C1-C4: PE8 / PE9 / PE11 / PE12 （输入上拉，读低电平=按下）
 *
 *  扫描流程：
 *    逐行拉低（其余行拉高）-> 列稳定延时 -> 读4列 -> 恢复该行拉高
 *    任一列为低 => 按 KEY_MAP[row][col] 返回键值
 *
 *  并发安全：
 *    本驱动不操作共享外设，任务间天然安全；扫描期间 SHT30/QMI8658/INA226
 *    的 I2C 事务与 OLED 写屏互不干扰。
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_KEY_MATRIX_ENABLE

#include "bsp_key_matrix.h"
#include "main.h"
#include "bsp_delay.h"

/* ========== 引脚映射 ========== */
#define KEYM_ROW_PORT   GPIOE
#define KEYM_COL_PORT   GPIOE

static const uint16_t s_row_pins[4] = {
    GPIO_PIN_2,   /* R1 */
    GPIO_PIN_6,   /* R2 */
    GPIO_PIN_7,   /* R3 */
    GPIO_PIN_10   /* R4 */
};
static const uint16_t s_col_pins[4] = {
    GPIO_PIN_8,   /* C1 */
    GPIO_PIN_9,   /* C2 */
    GPIO_PIN_11,  /* C3 */
    GPIO_PIN_12   /* C4 */
};

/* 键值映射表：KEY_MAP[row][col] */
static const uint8_t s_key_map[4][4] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' }
};

/* ==========================================================================
 *  初始化
 * ========================================================================== */
void BSP_KeyMatrix_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* 行：推挽输出，初始高电平（不选中任何行） */
    GPIO_InitStruct.Pin   = GPIO_PIN_2 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_10;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(KEYM_ROW_PORT, &GPIO_InitStruct);

    /* 列：输入上拉 */
    GPIO_InitStruct.Pin   = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(KEYM_COL_PORT, &GPIO_InitStruct);

    /* 全部行拉高（空闲状态） */
    for (i = 0U; i < 4U; i++) {
        HAL_GPIO_WritePin(KEYM_ROW_PORT, s_row_pins[i], GPIO_PIN_SET);
    }
}

/* ==========================================================================
 *  单次扫描
 * ========================================================================== */
uint8_t BSP_KeyMatrix_Scan(void)
{
    uint8_t row;
    uint8_t col;

    for (row = 0U; row < 4U; row++) {
        /* 1. 当前行拉低，其余行拉高 */
        HAL_GPIO_WritePin(KEYM_ROW_PORT, s_row_pins[row], GPIO_PIN_RESET);

        /* 2. 列线电平稳定（含走线/上拉RC，1ms 足够） */
        BSP_DelayUs(1000U);

        /* 3. 读4列：任一列为低 => 该行该列按下 */
        for (col = 0U; col < 4U; col++) {
            if (HAL_GPIO_ReadPin(KEYM_COL_PORT, s_col_pins[col]) == GPIO_PIN_RESET) {
                /* 恢复该行拉高，再返回（避免按键持续按下时反复触发，消抖在APP层） */
                HAL_GPIO_WritePin(KEYM_ROW_PORT, s_row_pins[row], GPIO_PIN_SET);
                return s_key_map[row][col];
            }
        }

        /* 4. 恢复该行拉高，扫描下一行 */
        HAL_GPIO_WritePin(KEYM_ROW_PORT, s_row_pins[row], GPIO_PIN_SET);
    }

    return 0U;   /* 无按键 */
}

#endif /* BSP_KEY_MATRIX_ENABLE */
