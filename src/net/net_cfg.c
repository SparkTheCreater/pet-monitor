/**
 * @file    net_cfg.c
 * @brief   只读相机段里的地址、超时和传输方式。
 */
#include "net_cfg.h"

#include <string.h>

static char Lower(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int Eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
    {
        return 0;
    }
    while (*a != '\0' && Lower(*a) == Lower(*b))
    {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int IsRtsp(const char *url)
{
    static const char kPrefix[] = "rtsp://";
    int i;
    for (i = 0; kPrefix[i] != '\0'; i++)
    {
        if (Lower(url[i]) != kPrefix[i])
        {
            return 0;
        }
    }
    return 1;
}

static BaseStatus SourceName(const char *section, char *dst, size_t dst_size)
{
    const char *suffix;
    size_t n;
    if (strncmp(section, "camera.", 7) != 0)
    {
        return BASE_ERR;
    }
    suffix = section + 7;
    if (suffix[0] == '\0' || strchr(suffix, '.') != NULL)
    {
        return BASE_ERR;
    }
    n = strlen(suffix);
    if (n + 1 > dst_size)
    {
        return BASE_ERR;
    }
    memcpy(dst, suffix, n + 1);
    return BASE_OK;
}

static BaseStatus TransportOf(const char *text, int *out)
{
    if (Eq(text, "tcp"))
    {
        *out = NET_TRANSPORT_TCP;
        return BASE_OK;
    }
    if (Eq(text, "udp"))
    {
        *out = NET_TRANSPORT_UDP;
        return BASE_OK;
    }
    return BASE_ERR;
}

static BaseStatus OneSource(const BaseIni *ini, const char *section, int shared_timeout, NetSource *src)
{
    memset(src, 0, sizeof(*src));
    if (SourceName(section, src->name, sizeof(src->name)) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (!base_ini_has(ini, section, "url"))
    {
        return BASE_ERR;
    }
    if (base_ini_copy(ini, section, "url", "", src->url, sizeof(src->url)) != BASE_OK ||
        src->url[0] == '\0' || !IsRtsp(src->url))
    {
        return BASE_ERR;
    }
    if (base_ini_has(ini, section, "timeout_ms"))
    {
        if (base_ini_int(ini, section, "timeout_ms", shared_timeout, &src->timeout_ms) != BASE_OK)
        {
            return BASE_ERR;
        }
    }
    else
    {
        src->timeout_ms = shared_timeout;
    }
    if (src->timeout_ms <= 0)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static BaseStatus Sources(const BaseIni *ini, NetConfig *cfg)
{
    const char *name[NET_SOURCE_MAX];
    int shared = 5000;
    int n;
    int i;
    if (base_ini_int(ini, "camera", "timeout_ms", 5000, &shared) != BASE_OK || shared <= 0)
    {
        return BASE_ERR;
    }
    if (TransportOf(base_ini_str(ini, "camera", "transport", "tcp"), &cfg->transport) != BASE_OK)
    {
        return BASE_ERR;
    }
    n = base_ini_sections(ini, "camera.", name, NET_SOURCE_MAX);
    if (n > NET_SOURCE_MAX)
    {
        return BASE_ERR;
    }
    for (i = 0; i < n; i++)
    {
        if (OneSource(ini, name[i], shared, &cfg->source[i]) != BASE_OK)
        {
            return BASE_ERR;
        }
    }
    cfg->source_count = n;
    return BASE_OK;
}

BaseStatus net_cfg_from_ini(const BaseIni *ini, NetConfig *out)
{
    NetConfig cfg;
    if (ini == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    memset(&cfg, 0, sizeof(cfg));
    if (Sources(ini, &cfg) != BASE_OK)
    {
        return BASE_ERR;
    }
    *out = cfg;
    return BASE_OK;
}
