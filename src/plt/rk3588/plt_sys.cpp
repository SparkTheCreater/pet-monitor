/**
 * @file    plt_sys.cpp
 * @brief   引用计数初始化。图像缓冲的分配组在第一次成功时建立。
 */
#include "plt/plt_sys.h"

#include "plt_internal.h"

#include <mutex>

namespace
{

std::mutex g_mutex;
int g_refs = 0;

}

extern "C" BaseStatus plt_init(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_refs > 0)
    {
        g_refs++;
        return BASE_OK;
    }
    if (plt_rk_mem_open() != BASE_OK)
    {
        return BASE_ERR;
    }
    g_refs = 1;
    return BASE_OK;
}

extern "C" void plt_deinit(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_refs <= 0)
    {
        return;
    }
    g_refs--;
    if (g_refs == 0)
    {
        plt_rk_mem_close();
    }
}
