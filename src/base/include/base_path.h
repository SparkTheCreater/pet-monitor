/**
 * @file    base_path.h
 * @brief   把相对路径接到根目录后面。以 / 开头的路径原样拷贝。
 */
#ifndef BASE_PATH_H
#define BASE_PATH_H

#include "base_image.h"

#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

static inline BaseStatus base_path_join(const char *root, const char *path, char *out, size_t out_size)
{
    size_t root_len;
    size_t path_len;
    int slash;
    if (path == NULL || path[0] == '\0' || out == NULL || out_size == 0)
    {
        return BASE_ERR;
    }
    if (path[0] == '/' || root == NULL || root[0] == '\0')
    {
        path_len = strlen(path);
        if (path_len + 1 > out_size)
        {
            return BASE_ERR;
        }
        memcpy(out, path, path_len + 1);
        return BASE_OK;
    }
    root_len = strlen(root);
    path_len = strlen(path);
    slash = root[root_len - 1] != '/';
    if (root_len + (size_t)slash + path_len + 1 > out_size)
    {
        return BASE_ERR;
    }
    memcpy(out, root, root_len);
    if (slash)
    {
        out[root_len] = '/';
    }
    memcpy(out + root_len + (size_t)slash, path, path_len + 1);
    return BASE_OK;
}

#ifdef __cplusplus
}
#endif

#endif
