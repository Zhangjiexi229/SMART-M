/*----------------------------------------------------------------------------/
/  FatFs - Generic FAT file system module  R0.12c  disk I/O layer
/  SMART-M 定制：SDIO 4-bit SD/TF 卡（bsp_sd.c）作为物理驱动
/-----------------------------------------------------------------------------/
/  说明：
/    1. 仅使用逻辑驱动器 0（对应 bsp_sd 的 SD/TF 卡）。
/    2. 未对齐缓冲区（非 4 字节对齐）自动经 512B 对齐暂存区中转，
/       满足 HAL_SD_ReadBlocks / HAL_SD_WriteBlocks 的对齐要求。
/    3. _FS_NORTC=1 时 get_fattime 不会被调用，此处仍保留实现以便将来启用 RTC。
/----------------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk I/O functions */
#include "bsp_sd.h"
#include <string.h>

/* 对齐暂存区：512B = 1 扇区，用于非对齐缓冲区的读写中转 */
static uint32_t s_align_buf[128] __attribute__((aligned(4)));

/*----------------------------------------------------------------------------*/
/* 获取 SD 卡就绪状态（FatFs 轮询用）                                          */
/*----------------------------------------------------------------------------*/
DSTATUS disk_status(
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv != 0U) {
		return STA_NOINIT;
	}
	if (BSP_SD_IsReady() != 0U) {
		return 0U;	/* 卡已初始化，处于就绪 */
	}
	return STA_NOINIT;
}

/*----------------------------------------------------------------------------*/
/* 初始化 SD 卡                                                               */
/*----------------------------------------------------------------------------*/
DSTATUS disk_initialize(
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv != 0U) {
		return STA_NOINIT;
	}
	/* 幂等：SDIO 已初始化（BSP_SD_Init 成功过）则直接返回，
	 * 避免 f_mount 每次重复跑完整 SDIO 初始化（阻塞 1~2s 会拖垮喂狗节奏） */
	if (BSP_SD_IsReady() != 0U) {
		return 0U;
	}
	if (BSP_SD_Init() != 0U) {
		return STA_NOINIT;
	}
	return 0U;
}

/*----------------------------------------------------------------------------*/
/* 读扇区                                                                     */
/*----------------------------------------------------------------------------*/
DRESULT disk_read(
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	/* Sector address in LBA */
	UINT count		/* Number of sectors to read */
)
{
	UINT done = 0U;

	if (pdrv != 0U) {
		return RES_PARERR;
	}
	if (((uint32_t)buff & 0x3U) != 0U) {
		/* 非 4 字节对齐：逐扇区经对齐暂存区中转 */
		while (done < count) {
			if (BSP_SD_ReadBlocks(s_align_buf, sector + done, 1U, 1000U) != 0U) {
				return RES_ERROR;
			}
			memcpy(buff + done * 512U, s_align_buf, 512U);
			done++;
		}
		return RES_OK;
	}
	if (BSP_SD_ReadBlocks((uint32_t *)buff, sector, count, 1000U) != 0U) {
		return RES_ERROR;
	}
	return RES_OK;
}

/*----------------------------------------------------------------------------*/
/* 写扇区                                                                     */
/*----------------------------------------------------------------------------*/
#if _FS_READONLY == 0
DRESULT disk_write(
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Sector address in LBA */
	UINT count			/* Number of sectors to write */
)
{
	UINT done = 0U;

	if (pdrv != 0U) {
		return RES_PARERR;
	}
	if (((uint32_t)buff & 0x3U) != 0U) {
		/* 非 4 字节对齐：逐扇区经对齐暂存区中转 */
		while (done < count) {
			memcpy(s_align_buf, buff + done * 512U, 512U);
			if (BSP_SD_WriteBlocks(s_align_buf, sector + done, 1U, 500U) != 0U) {
				return RES_ERROR;
			}
			done++;
		}
		return RES_OK;
	}
	if (BSP_SD_WriteBlocks((uint32_t *)buff, sector, count, 500U) != 0U) {
		return RES_ERROR;
	}
	return RES_OK;
}
#endif

/*----------------------------------------------------------------------------*/
/* 控制命令                                                                   */
/*----------------------------------------------------------------------------*/
DRESULT disk_ioctl(
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	BSP_SD_CardInfo_t info;

	if (pdrv != 0U) {
		return RES_PARERR;
	}

	switch (cmd) {
	case CTRL_SYNC:		/* 等待未完成的写操作完成 */
		if (BSP_SD_Sync() != 0U) {
			return RES_ERROR;
		}
		return RES_OK;

	case GET_SECTOR_COUNT:	/* 获取总扇区数（_USE_MKFS 需要） */
		if (BSP_SD_GetCardInfo(&info) != 0U) {
			return RES_ERROR;
		}
		*(DWORD *)buff = (DWORD)info.block_count;
		return RES_OK;

	case GET_SECTOR_SIZE:	/* 获取扇区大小 */
		*(WORD *)buff = 512U;
		return RES_OK;

	case GET_BLOCK_SIZE:	/* 获取擦除块大小（扇区为单位，_USE_MKFS 需要） */
		*(DWORD *)buff = 1U;
		return RES_OK;

	default:
		return RES_PARERR;
	}
}

/*----------------------------------------------------------------------------*/
/* 获取当前时间（FAT 时间戳）                                                  */
/* 说明：_FS_NORTC=1 时不会调用；此实现返回编译期固定日期，启用 RTC 后可改写     */
/*----------------------------------------------------------------------------*/
DWORD get_fattime(void)
{
	return ((DWORD)(2026 - 1980) << 25)	/* 年 = 2026 */
	     | ((DWORD)9 << 21)				/* 月 = 9 */
	     | ((DWORD)14 << 16)			/* 日 = 14 */
	     | ((DWORD)0 << 11)				/* 时 = 0 */
	     | ((DWORD)0 << 5)				/* 分 = 0 */
	     | ((DWORD)0);					/* 秒/2 = 0 */
}
