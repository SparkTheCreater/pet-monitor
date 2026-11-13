/**
 * @file    app_cfg.c
 * @brief   读 [base]、[camera]、[camera.*]、[event]、[email]。
 */
#include "app_cfg.h"

#include <string.h>

static int Eq(const char *a, const char *b);

static BaseStatus AlgoOf(const char *text, int *out)
{
    if (text == NULL || text[0] == '\0' || Eq(text, "cat"))
    {
        *out = ALGO_CAT_DET;
        return BASE_OK;
    }
    if (Eq(text, "detect"))
    {
        *out = ALGO_DETECT;
        return BASE_OK;
    }
    if (Eq(text, "track"))
    {
        *out = ALGO_TRACK;
        return BASE_OK;
    }
    if (Eq(text, "recognize"))
    {
        *out = ALGO_RECOGNIZE;
        return BASE_OK;
    }
    return BASE_ERR;
}

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

static void Wire(AppConfig *cfg)
{
    cfg->email.host = cfg->host;
    cfg->email.username = cfg->user;
    cfg->email.password = cfg->password;
    cfg->email.ca_file = cfg->ca[0] != '\0' ? cfg->ca : NULL;
    cfg->email.trace = NULL;
    cfg->email.trace_user = NULL;
}

static BaseStatus CameraName(const char *section, char *dst, size_t dst_size)
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

static BaseStatus PlaceOf(const char *text, int *out)
{
    if (Eq(text, "eat"))
    {
        *out = APP_PLACE_EAT;
        return BASE_OK;
    }
    if (Eq(text, "litter"))
    {
        *out = APP_PLACE_LITTER;
        return BASE_OK;
    }
    return BASE_ERR;
}

static BaseStatus ScoreOf(const BaseIni *ini, const char *section, double fallback, float *out)
{
    double score;
    if (base_ini_has(ini, section, "score_min"))
    {
        if (base_ini_real(ini, section, "score_min", fallback, &score) != BASE_OK)
        {
            return BASE_ERR;
        }
    }
    else
    {
        score = fallback;
    }
    if (score <= 0.0 || score > 1.0)
    {
        return BASE_ERR;
    }
    *out = (float)score;
    return BASE_OK;
}

static BaseStatus OneCamera(const BaseIni *ini, const char *section, int shared_timeout, double score, AppCamera *cam)
{
    memset(cam, 0, sizeof(*cam));
    if (CameraName(section, cam->name, sizeof(cam->name)) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (!base_ini_has(ini, section, "url"))
    {
        return BASE_ERR;
    }
    if (base_ini_copy(ini, section, "url", "", cam->url, sizeof(cam->url)) != BASE_OK || cam->url[0] == '\0')
    {
        return BASE_ERR;
    }
    if (PlaceOf(base_ini_str(ini, section, "place", NULL), &cam->place) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (base_ini_has(ini, section, "timeout_ms"))
    {
        if (base_ini_int(ini, section, "timeout_ms", shared_timeout, &cam->timeout_ms) != BASE_OK)
        {
            return BASE_ERR;
        }
    }
    else
    {
        cam->timeout_ms = shared_timeout;
    }
    if (cam->timeout_ms <= 0)
    {
        return BASE_ERR;
    }
    if (AlgoOf(base_ini_str(ini, section, "algo", "cat"), &cam->algo) != BASE_OK)
    {
        return BASE_ERR;
    }
    return ScoreOf(ini, section, score, &cam->score_min);
}

static BaseStatus Cameras(const BaseIni *ini, AppConfig *cfg)
{
    const char *name[APP_CAMERA_MAX];
    int shared = 5000;
    double score = 0.25;
    int n;
    int i;
    if (base_ini_int(ini, "camera", "timeout_ms", 5000, &shared) != BASE_OK || shared <= 0)
    {
        return BASE_ERR;
    }
    if (base_ini_real(ini, "algo", "score_min", 0.25, &score) != BASE_OK || score <= 0.0 || score > 1.0)
    {
        return BASE_ERR;
    }
    n = base_ini_sections(ini, "camera.", name, APP_CAMERA_MAX);
    if (n > APP_CAMERA_MAX)
    {
        return BASE_ERR;
    }
    for (i = 0; i < n; i++)
    {
        if (OneCamera(ini, name[i], shared, score, &cfg->camera[i]) != BASE_OK)
        {
            return BASE_ERR;
        }
    }
    cfg->camera_count = n;
    return BASE_OK;
}

static BaseStatus Positive(const BaseIni *ini, const char *key, int def, int *out)
{
    if (base_ini_int(ini, "event", key, def, out) != BASE_OK || *out <= 0)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static BaseStatus TlsOf(const char *text, int *out)
{
    if (Eq(text, "auto"))
    {
        *out = APP_EMAIL_TLS_AUTO;
        return BASE_OK;
    }
    if (Eq(text, "implicit"))
    {
        *out = APP_EMAIL_TLS_IMPLICIT;
        return BASE_OK;
    }
    if (Eq(text, "starttls"))
    {
        *out = APP_EMAIL_TLS_STARTTLS;
        return BASE_OK;
    }
    if (Eq(text, "none"))
    {
        *out = APP_EMAIL_TLS_NONE;
        return BASE_OK;
    }
    return BASE_ERR;
}

static BaseStatus Email(const BaseIni *ini, AppConfig *cfg)
{
    if (base_ini_bool(ini, "email", "enable", 0, &cfg->email_enable) != BASE_OK ||
        base_ini_bool(ini, "email", "insecure", 0, &cfg->email.insecure) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (base_ini_copy(ini, "email", "host", "smtp.qq.com", cfg->host, sizeof(cfg->host)) != BASE_OK ||
        base_ini_copy(ini, "email", "username", "", cfg->user, sizeof(cfg->user)) != BASE_OK ||
        base_ini_copy(ini, "email", "password", "", cfg->password, sizeof(cfg->password)) != BASE_OK ||
        base_ini_copy(ini, "email", "from", "", cfg->from, sizeof(cfg->from)) != BASE_OK ||
        base_ini_copy(ini, "email", "to", "", cfg->to, sizeof(cfg->to)) != BASE_OK ||
        base_ini_copy(ini, "email", "ca_file", "", cfg->ca, sizeof(cfg->ca)) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (base_ini_int(ini, "email", "port", 465, &cfg->email.port) != BASE_OK ||
        cfg->email.port < 0 || cfg->email.port > 65535)
    {
        return BASE_ERR;
    }
    if (base_ini_int(ini, "email", "timeout_ms", 30000, &cfg->email.timeout_ms) != BASE_OK ||
        cfg->email.timeout_ms < 0)
    {
        return BASE_ERR;
    }
    if (TlsOf(base_ini_str(ini, "email", "tls", "implicit"), &cfg->email.tls) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (cfg->email_enable && (cfg->host[0] == '\0' || cfg->from[0] == '\0' || cfg->to[0] == '\0'))
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

BaseStatus app_cfg_from_ini(const BaseIni *ini, AppConfig *out)
{
    AppConfig cfg;
    if (ini == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    memset(&cfg, 0, sizeof(cfg));
    if (base_ini_copy(ini, "base", "dir", "/opt/pet-monitor", cfg.dir, sizeof(cfg.dir)) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (Cameras(ini, &cfg) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (Positive(ini, "eat_sec", 3, &cfg.eat_sec) != BASE_OK ||
        Positive(ini, "litter_sec", 5, &cfg.litter_sec) != BASE_OK ||
        Positive(ini, "clip_sec", 10, &cfg.clip_sec) != BASE_OK ||
        Positive(ini, "present_sec", 20, &cfg.present_sec) != BASE_OK ||
        Positive(ini, "shot_sec", 10, &cfg.shot_sec) != BASE_OK ||
        Positive(ini, "absent_sec", 20, &cfg.absent_sec) != BASE_OK ||
        cfg.shot_sec >= cfg.present_sec)
    {
        return BASE_ERR;
    }
    if (Email(ini, &cfg) != BASE_OK)
    {
        return BASE_ERR;
    }
    *out = cfg;
    Wire(out);
    return BASE_OK;
}

int app_cfg_camera_algo(const AppCamera *camera)
{
    if (camera == NULL)
    {
        return ALGO_CAT_DET;
    }
    return camera->algo;
}

float app_cfg_camera_score(const AppCamera *camera)
{
    if (camera == NULL)
    {
        return 0.25f;
    }
    return camera->score_min;
}
