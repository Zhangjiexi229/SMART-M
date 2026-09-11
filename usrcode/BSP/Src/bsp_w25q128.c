/**
 ******************************************************************************
 * @file    bsp_w25q128.c
 * @brief   W25Q128 SPI Flash 板级驱动 — 软件 SPI (Mode0, MSB first)
 *
 *  引脚分配：
 *    PB3  = SPI_SCLK  (推挽输出)
 *    PB5  = SPI_MOSI  (推挽输出)
 *    PB4  = SPI_MISO  (输入上拉)
 *    PG10 = SPI_NSS   (推挽输出, 低有效)
 *
 *  功能：
 *    - JEDEC ID / Device ID 读取
 *    - 状态寄存器读写
 *    - 写使能/禁用
 *    - 扇区(4KB)/块(32KB/64KB)/全片擦除
 *    - 页编程(256B) + 跨页自动分片写入
 *    - 任意地址连续读取
 *    - 综合自检 SelfTest
 ******************************************************************************
 */
#include "module_cfg.h"
#if BSP_W25Q128_ENABLE

#include "bsp_w25q128.h"
#include "bsp_delay.h"
#include "bsp_uart.h"
#include "main.h"

/* ==================== 内部宏 ==================== */

#define W25Q128_CMD_WRITE_ENABLE       0x06U
#define W25Q128_CMD_WRITE_DISABLE      0x04U
#define W25Q128_CMD_READ_STATUS1       0x05U
#define W25Q128_CMD_READ_STATUS2       0x35U
#define W25Q128_CMD_WRITE_STATUS       0x01U
#define W25Q128_CMD_PAGE_PROGRAM       0x02U
#define W25Q128_CMD_READ_DATA          0x03U
#define W25Q128_CMD_FAST_READ          0x0BU
#define W25Q128_CMD_SECTOR_ERASE       0x20U
#define W25Q128_CMD_BLOCK32_ERASE      0x52U
#define W25Q128_CMD_BLOCK64_ERASE      0xD8U
#define W25Q128_CMD_CHIP_ERASE         0xC7U
#define W25Q128_CMD_JEDEC_ID           0x9FU
#define W25Q128_CMD_DEVICE_ID          0xABU
#define W25Q128_CMD_POWER_DOWN         0xB9U
#define W25Q128_CMD_RELEASE_PD         0xABU

#define W25Q128_PAGE_SIZE               256U
#define W25Q128_SECTOR_SIZE             4096U
#define W25Q128_TIMEOUT_MS              5000U

/* ==================== 软件 SPI 底层 ==================== */

static inline void W25Q128_NSS_Low(void)  { HAL_GPIO_WritePin(SPI_NSS_GPIO_Port,  SPI_NSS_Pin,  GPIO_PIN_RESET); }
static inline void W25Q128_NSS_High(void) { HAL_GPIO_WritePin(SPI_NSS_GPIO_Port,  SPI_NSS_Pin,  GPIO_PIN_SET);   }
static inline void W25Q128_CLK_Low(void)  { HAL_GPIO_WritePin(SPI_SCLK_GPIO_Port,  SPI_SCLK_Pin,  GPIO_PIN_RESET); }
static inline void W25Q128_CLK_High(void) { HAL_GPIO_WritePin(SPI_SCLK_GPIO_Port,  SPI_SCLK_Pin,  GPIO_PIN_SET);   }
static inline void W25Q128_MOSI_Low(void) { HAL_GPIO_WritePin(SPI_MOSI_GPIO_Port, SPI_MOSI_Pin, GPIO_PIN_RESET); }
static inline void W25Q128_MOSI_High(void){ HAL_GPIO_WritePin(SPI_MOSI_GPIO_Port, SPI_MOSI_Pin, GPIO_PIN_SET);   }
static inline uint8_t W25Q128_MISO_Read(void) { return (HAL_GPIO_ReadPin(SPI_MISO_GPIO_Port, SPI_MISO_Pin) == GPIO_PIN_SET) ? 1U : 0U; }

/**
 * @brief  软件 SPI 读写一个字节 (Mode0: CPOL=0, CPHA=0, MSB first)
 */
static uint8_t W25Q128_SPI_Transfer(uint8_t tx)
{
    uint8_t rx = 0U;
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        /* MOSI 在下降沿前稳定 (CPHA=0: 第一个边沿采样) */
        if (tx & 0x80U) { W25Q128_MOSI_High(); } else { W25Q128_MOSI_Low(); }
        tx <<= 1;
        BSP_DelayUs(1U);

        W25Q128_CLK_High();          /* 上升沿: 从机采样 MOSI */
        BSP_DelayUs(1U);

        rx <<= 1;
        if (W25Q128_MISO_Read()) { rx |= 0x01U; }  /* 主机在高电平期间读 MISO */

        W25Q128_CLK_Low();           /* 下降沿: 从机移位 MISO */
        BSP_DelayUs(1U);
    }

    return rx;
}

/**
 * @brief  发送 24 位地址
 */
static void W25Q128_SendAddr(uint32_t addr)
{
    (void)W25Q128_SPI_Transfer((uint8_t)((addr >> 16) & 0xFFU));
    (void)W25Q128_SPI_Transfer((uint8_t)((addr >> 8)  & 0xFFU));
    (void)W25Q128_SPI_Transfer((uint8_t)( addr        & 0xFFU));
}

/* ==================== GPIO 初始化 ==================== */

uint8_t BSP_W25Q128_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIOB 和 GPIOG 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* SCLK: PB3 推挽输出 */
    GPIO_InitStruct.Pin   = SPI_SCLK_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SPI_SCLK_GPIO_Port, &GPIO_InitStruct);

    /* MOSI: PB5 推挽输出 */
    GPIO_InitStruct.Pin = SPI_MOSI_Pin;
    HAL_GPIO_Init(SPI_MOSI_GPIO_Port, &GPIO_InitStruct);

    /* MISO: PB4 输入上拉 */
    GPIO_InitStruct.Pin   = SPI_MISO_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(SPI_MISO_GPIO_Port, &GPIO_InitStruct);

    /* NSS: PG10 推挽输出 */
    GPIO_InitStruct.Pin   = SPI_NSS_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SPI_NSS_GPIO_Port, &GPIO_InitStruct);

    /* 空闲状态：NSS高，CLK低，MOSI高 */
    W25Q128_NSS_High();
    W25Q128_CLK_Low();
    W25Q128_MOSI_High();
    BSP_DelayUs(10U);

    return 0U;
}

/* ==================== ID 读取 ==================== */

uint8_t BSP_W25Q128_ReadJEDEC_ID(uint8_t *manuf, uint8_t *type, uint8_t *cap)
{
    if ((manuf == NULL) || (type == NULL) || (cap == NULL)) return 1U;

    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_JEDEC_ID);
    *manuf = W25Q128_SPI_Transfer(0xFFU);
    *type  = W25Q128_SPI_Transfer(0xFFU);
    *cap   = W25Q128_SPI_Transfer(0xFFU);
    W25Q128_NSS_High();

    return 0U;
}

uint8_t BSP_W25Q128_ReadDeviceID(uint8_t *dev_id)
{
    if (dev_id == NULL) return 1U;

    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_DEVICE_ID);
    (void)W25Q128_SPI_Transfer(0x00U);  /* dummy 3 bytes */
    (void)W25Q128_SPI_Transfer(0x00U);
    (void)W25Q128_SPI_Transfer(0x00U);
    *dev_id = W25Q128_SPI_Transfer(0xFFU);
    W25Q128_NSS_High();

    return 0U;
}

/* ==================== 状态寄存器 ==================== */

uint8_t BSP_W25Q128_ReadStatus1(void)
{
    uint8_t sr;
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_READ_STATUS1);
    sr = W25Q128_SPI_Transfer(0xFFU);
    W25Q128_NSS_High();
    return sr;
}

uint8_t BSP_W25Q128_ReadStatus2(void)
{
    uint8_t sr;
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_READ_STATUS2);
    sr = W25Q128_SPI_Transfer(0xFFU);
    W25Q128_NSS_High();
    return sr;
}

uint8_t BSP_W25Q128_IsBusy(void)
{
    return (BSP_W25Q128_ReadStatus1() & W25Q128_SR1_BUSY) ? 1U : 0U;
}

uint8_t BSP_W25Q128_WaitBusy(uint32_t timeout_ms)
{
    uint32_t i;
    for (i = 0U; i < timeout_ms; i++) {
        if (!BSP_W25Q128_IsBusy()) return 0U;
        BSP_DelayMs(1U);
    }
    return 1U;
}

/* ==================== 写使能 ==================== */

void BSP_W25Q128_WriteEnable(void)
{
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_WRITE_ENABLE);
    W25Q128_NSS_High();
}

void BSP_W25Q128_WriteDisable(void)
{
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_WRITE_DISABLE);
    W25Q128_NSS_High();
}

/* ==================== 擦除 ==================== */

uint8_t BSP_W25Q128_SectorErase(uint32_t addr)
{
    BSP_W25Q128_WriteEnable();
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_SECTOR_ERASE);
    W25Q128_SendAddr(addr);
    W25Q128_NSS_High();
    return BSP_W25Q128_WaitBusy(W25Q128_TIMEOUT_MS);
}

uint8_t BSP_W25Q128_Block32Erase(uint32_t addr)
{
    BSP_W25Q128_WriteEnable();
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_BLOCK32_ERASE);
    W25Q128_SendAddr(addr);
    W25Q128_NSS_High();
    return BSP_W25Q128_WaitBusy(W25Q128_TIMEOUT_MS);
}

uint8_t BSP_W25Q128_Block64Erase(uint32_t addr)
{
    BSP_W25Q128_WriteEnable();
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_BLOCK64_ERASE);
    W25Q128_SendAddr(addr);
    W25Q128_NSS_High();
    return BSP_W25Q128_WaitBusy(W25Q128_TIMEOUT_MS);
}

uint8_t BSP_W25Q128_ChipErase(void)
{
    BSP_W25Q128_WriteEnable();
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_CHIP_ERASE);
    W25Q128_NSS_High();
    return BSP_W25Q128_WaitBusy(60000U);  /* 全片擦除最长约 30s */
}

/* ==================== 读取 ==================== */

uint8_t BSP_W25Q128_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (buf == NULL) return 1U;
    if (len == 0U) return 0U;

    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_READ_DATA);
    W25Q128_SendAddr(addr);
    for (i = 0U; i < len; i++) {
        buf[i] = W25Q128_SPI_Transfer(0xFFU);
    }
    W25Q128_NSS_High();

    return 0U;
}

/* ==================== 页编程 ==================== */

/**
 * @brief  单页编程 (不超过 256 字节, 不跨页)
 */
static uint8_t W25Q128_PageProgram(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (buf == NULL) return 1U;
    if (len == 0U) return 0U;
    if (len > W25Q128_PAGE_SIZE) return 1U;

    BSP_W25Q128_WriteEnable();
    W25Q128_NSS_Low();
    (void)W25Q128_SPI_Transfer(W25Q128_CMD_PAGE_PROGRAM);
    W25Q128_SendAddr(addr);
    for (i = 0U; i < len; i++) {
        (void)W25Q128_SPI_Transfer(buf[i]);
    }
    W25Q128_NSS_High();

    return BSP_W25Q128_WaitBusy(W25Q128_TIMEOUT_MS);
}

/**
 * @brief  任意地址写入, 自动按页分片 (跨页边界自动切分)
 */
uint8_t BSP_W25Q128_Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t offset = 0U;
    uint32_t chunk;

    if (buf == NULL) return 1U;
    if (len == 0U) return 0U;

    while (offset < len) {
        /* 当前页剩余空间 */
        chunk = W25Q128_PAGE_SIZE - ((addr + offset) % W25Q128_PAGE_SIZE);
        if (chunk > (len - offset)) chunk = len - offset;

        if (W25Q128_PageProgram(addr + offset, &buf[offset], chunk) != 0U) {
            return 1U;
        }
        offset += chunk;
    }

    return 0U;
}

/* ==================== 辅助 ==================== */

uint8_t BSP_W25Q128_IsErased(uint32_t addr, uint32_t len)
{
    uint32_t i;
    uint8_t  buf[64];
    uint32_t chunk;

    while (len > 0U) {
        chunk = (len > 64U) ? 64U : len;
        if (BSP_W25Q128_Read(addr, buf, chunk) != 0U) return 0U;
        for (i = 0U; i < chunk; i++) {
            if (buf[i] != 0xFFU) return 0U;
        }
        addr += chunk;
        len  -= chunk;
    }
    return 1U;
}

/* ==================== 综合自检 ==================== */

uint8_t BSP_W25Q128_SelfTest(void)
{
    uint8_t manuf, type, cap;
    uint8_t status;
    uint8_t fail = 0U;
    uint8_t wbuf[32];
    uint8_t rbuf[32];
    uint32_t i;
    const uint32_t test_addr = 0x000000U;

    BSP_UART1_Printf("\r\n=== W25Q128 SelfTest ===\r\n");

    /* 1. 读取 JEDEC ID */
    if (BSP_W25Q128_ReadJEDEC_ID(&manuf, &type, &cap) != 0U) {
        BSP_UART1_Printf("[FAIL] JEDEC ID read error\r\n");
        fail |= 0x01U;
    } else {
        BSP_UART1_Printf("[ID] Manuf=0x%02X Type=0x%02X Cap=0x%02X", manuf, type, cap);
        if ((manuf == W25Q128_JEDEC_MANUFACTURER) &&
            (type  == W25Q128_JEDEC_MEMORY_TYPE) &&
            (cap   == W25Q128_JEDEC_CAPACITY)) {
            BSP_UART1_Printf(" (W25Q128, 16MB) OK\r\n");
        } else {
            BSP_UART1_Printf(" (expected EF 40 18) FAIL\r\n");
            fail |= 0x01U;
        }
    }

    /* 2. 读取状态寄存器1 */
    status = BSP_W25Q128_ReadStatus1();
    BSP_UART1_Printf("[SR1] 0x%02X (BUSY=%d WEL=%d)\r\n", status,
                      (status & W25Q128_SR1_BUSY) ? 1 : 0,
                      (status & W25Q128_SR1_WEL) ? 1 : 0);

    /* 3. 擦除扇区0 */
    BSP_UART1_Printf("[Erase] Sector 0 (4KB)... ");
    if (BSP_W25Q128_SectorErase(test_addr) != 0U) {
        BSP_UART1_Printf("FAIL (timeout)\r\n");
        fail |= 0x02U;
    } else {
        BSP_UART1_Printf("OK\r\n");
    }

    /* 4. 验证擦除后前32字节全0xFF */
    BSP_UART1_Printf("[Check] Erased (first 32B)... ");
    if (BSP_W25Q128_IsErased(test_addr, 32U)) {
        BSP_UART1_Printf("OK (all 0xFF)\r\n");
    } else {
        BSP_UART1_Printf("FAIL (not all 0xFF)\r\n");
        fail |= 0x02U;
    }

    /* 5. 准备测试数据并写入 */
    for (i = 0U; i < 32U; i++) {
        wbuf[i] = (uint8_t)(0xA0 + i);
    }
    wbuf[0] = 'W'; wbuf[1] = '2'; wbuf[2] = '5'; wbuf[3] = 'Q';
    wbuf[4] = '1'; wbuf[5] = '2'; wbuf[6] = '8'; wbuf[7] = ' ';
    wbuf[8] = 'O'; wbuf[9] = 'K'; wbuf[10] = '!'; wbuf[11] = 0x00;

    BSP_UART1_Printf("[Write] 32B @ 0x%06X... ", (unsigned)test_addr);
    if (BSP_W25Q128_Write(test_addr, wbuf, 32U) != 0U) {
        BSP_UART1_Printf("FAIL\r\n");
        fail |= 0x04U;
    } else {
        BSP_UART1_Printf("OK\r\n");
    }

    /* 6. 回读并比对 */
    BSP_UART1_Printf("[Read]  32B @ 0x%06X: ", (unsigned)test_addr);
    if (BSP_W25Q128_Read(test_addr, rbuf, 32U) != 0U) {
        BSP_UART1_Printf("FAIL (read error)\r\n");
        fail |= 0x08U;
    } else {
        uint8_t mismatch = 0U;
        for (i = 0U; i < 16U; i++) {
            BSP_UART1_Printf("%02X ", rbuf[i]);
        }
        BSP_UART1_Printf("... ");
        for (i = 0U; i < 32U; i++) {
            if (rbuf[i] != wbuf[i]) { mismatch = 1U; break; }
        }
        if (mismatch) {
            BSP_UART1_Printf("FAIL (mismatch at byte %u: w=0x%02X r=0x%02X)\r\n",
                              (unsigned)i, wbuf[i], rbuf[i]);
            fail |= 0x08U;
        } else {
            BSP_UART1_Printf("OK (match)\r\n");
        }
    }

    /* 7. 总结 */
    BSP_UART1_Printf("=== W25Q128 SelfTest: %s ===\r\n\r\n",
                      (fail == 0U) ? "ALL PASSED" : "FAILED");

    return fail;
}

#endif /* BSP_W25Q128_ENABLE */
