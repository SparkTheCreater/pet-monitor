/**
 * @file    plt_internal.h
 * @brief   rk3588 各文件之间的内部接口，不对外。
 */
#ifndef PLT_INTERNAL_H
#define PLT_INTERNAL_H

#include "base_image.h"

/* plt_init 已经成功。只给本目录的实现用。 */
int plt_rk_ready(void);

/* 只由 plt_init / plt_deinit 在持锁时调用。 */
int plt_rk_mem_open(void);
void plt_rk_mem_close(void);

/* 实际用上的 MPP 缓冲类型，含 DMA32 标志。分配组还没建时返回 0。 */
int plt_rk_dma_type(void);

#endif
