/**
 ******************************************************************************
 * @file    bsp_key_matrix.h
 * @brief   4x4矩阵键盘驱动头文件 — GPIO行扫描+列读取
 *
 *  硬件连接（GPIOE，动态初始化，无需CubeMX重生成）：
 *    行(输出,拉低扫描): PE2 / PE6 / PE7 / PE10   （R1-R4）
 *    列(输入,上拉):     PE8 / PE9 / PE11 / PE12（C1-C4）
 *    注：PE3/PE4 为新核心板板载按键(KEY1/KEY0)、PE5 预留给 HC-SR04，均避开
 *
 *  键值映射（KEY_MAP[row][col]，行=输出端，列=输入端）：
 *    R1: '1' '2' '3' 'A'
 *    R2: '4' '5' '6' 'B'
 *    R3: '7' '8' '9' 'C'
 *    R4: '*' '0' '#' 'D'
 *
 *  设计要点：
 *    - 本驱动只做"单次扫描"，消抖与按键动作在 APP 层（app_key_matrix）完成
 *    - 行扫描时当前行拉低、其余行拉高，列读低电平即按下
 *    - 与 bsp_key（4个独立按键）引脚不同，互不影响，可并存
 ******************************************************************************
 */
#ifndef BSP_KEY_MATRIX_H
#define BSP_KEY_MATRIX_H

#include <stdint.h>

/* ========== 键值定义（与 KEY_MAP 对应，供 APP 层 switch 使用） ========== */
#define KEYM_KEY_1    '1'
#define KEYM_KEY_2    '2'
#define KEYM_KEY_3    '3'
#define KEYM_KEY_4    '4'
#define KEYM_KEY_5    '5'
#define KEYM_KEY_6    '6'
#define KEYM_KEY_7    '7'
#define KEYM_KEY_8    '8'
#define KEYM_KEY_9    '9'
#define KEYM_KEY_0    '0'
#define KEYM_KEY_A    'A'
#define KEYM_KEY_B    'B'
#define KEYM_KEY_C    'C'
#define KEYM_KEY_D    'D'
#define KEYM_KEY_STAR '*'   /* 星号键：进入/退出阈值设置 */
#define KEYM_KEY_POUND '#'  /* 井号键：数值加 */

/**
 * @brief  矩阵键盘 GPIO 初始化（行=输出推挽，列=输入上拉）
 * @note   在 APP_Init 中、调度器启动前调用
 */
void BSP_KeyMatrix_Init(void);

/**
 * @brief  单次扫描，返回当前按下的键值
 * @retval 按下的键字符（'1'~'9','0','A','B','C','D','*','#'），
 *         无按键按下时返回 0
 * @note   同一时刻只支持单键；扫描耗时约 4~8ms（4行×1ms列稳定+读列）
 */
uint8_t BSP_KeyMatrix_Scan(void);

#endif /* BSP_KEY_MATRIX_H */
