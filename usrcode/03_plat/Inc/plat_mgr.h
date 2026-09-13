/**
 ******************************************************************************
 * @file    plat_mgr.h
 * @brief   平台抽象层（PLAT）— 管理器（device manager / service manager）
 *
 *  统一生命周期原语：
 *    register → init → start → process → stop → deinit
 *  管理方式：
 *    - 设备/服务注册进管理器链表，按名查找
 *    - 批量驱动：init_all / start_all / process_all / stop_all / deinit_all
 *    - 服务启动前校验依赖设备已就绪（依赖注入）
 ******************************************************************************
 */
#ifndef PLAT_MGR_H
#define PLAT_MGR_H

#include "plat_obj.h"

/* ========== 设备管理器（device manager） ========== */
uint8_t     plat_devmgr_register(plat_dev_t *dev);   /* 注册设备（重名拒绝） */
uint8_t     plat_devmgr_unregister(plat_dev_t *dev); /* 注销设备 */
plat_dev_t *plat_devmgr_find(const char *name);      /* 按名查找设备 */
uint16_t    plat_devmgr_count(void);                 /* 已注册设备数 */

/* 单设备生命周期原语（对齐统一生命周期） */
uint8_t plat_dev_register(plat_dev_t *dev);
uint8_t plat_dev_init(plat_dev_t *dev);
uint8_t plat_dev_start(plat_dev_t *dev);
uint8_t plat_dev_process(plat_dev_t *dev);
uint8_t plat_dev_stop(plat_dev_t *dev);
uint8_t plat_dev_deinit(plat_dev_t *dev);

/* 批量驱动（返回失败个数，0=全部成功） */
uint8_t plat_devmgr_init_all(void);
uint8_t plat_devmgr_start_all(void);
uint8_t plat_devmgr_process_all(void);
uint8_t plat_devmgr_stop_all(void);
uint8_t plat_devmgr_deinit_all(void);

/* ========== 服务管理器（service manager） ========== */
uint8_t     plat_svcmgr_register(plat_svc_t *svc);   /* 注册服务（重名拒绝） */
uint8_t     plat_svcmgr_unregister(plat_svc_t *svc); /* 注销服务 */
plat_svc_t *plat_svcmgr_find(const char *name);      /* 按名查找服务 */
uint16_t    plat_svcmgr_count(void);                 /* 已注册服务数 */

uint8_t plat_svc_register(plat_svc_t *svc);
uint8_t plat_svc_init(plat_svc_t *svc);
uint8_t plat_svc_start(plat_svc_t *svc);   /* 启动前校验依赖设备已就绪 */
uint8_t plat_svc_process(plat_svc_t *svc);
uint8_t plat_svc_stop(plat_svc_t *svc);
uint8_t plat_svc_deinit(plat_svc_t *svc);

uint8_t plat_svcmgr_init_all(void);
uint8_t plat_svcmgr_start_all(void);
uint8_t plat_svcmgr_process_all(void);
uint8_t plat_svcmgr_stop_all(void);
uint8_t plat_svcmgr_deinit_all(void);

#endif /* PLAT_MGR_H */
