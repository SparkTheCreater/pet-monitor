/**
 * @file    media_sys.c
 * @brief   初始化交给平台层。
 */
#include "media_sys.h"

#include "plt/plt_sys.h"

BaseStatus media_init(void)
{
    return plt_init();
}

void media_deinit(void)
{
    plt_deinit();
}
