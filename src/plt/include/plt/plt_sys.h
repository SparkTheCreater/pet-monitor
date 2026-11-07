/**
 * @file    plt_sys.h
 * @brief   平台层初始化。用其它 plt 接口之前先 plt_init，用完 plt_deinit。
 */
#ifndef PM_PLT_SYS_H
#define PM_PLT_SYS_H

#include "pm_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 同步。可以重复调用，也可以多个线程同时调用。第一次才建立平台资源，之后只增加引用计数。
 * 失败时引用计数不变，调用方不要再配 plt_deinit。stub 在这里明确失败。
 */
PmStatus plt_init(void);

/* 引用计数减一，减到 0 才释放。没有对应的 plt_init 时直接返回。 */
void plt_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
