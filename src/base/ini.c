/**
 * @file    ini.c
 * @brief   一份文本，一张表。键和值都指向这文本里被截断的位置。
 */
#include "ini.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INI_MAX_BYTES (1024 * 1024)

typedef struct IniItem
{
    char *section;
    char *key;
    char *value;
}
IniItem;

struct BaseIni
{
    char *text;
    IniItem *item;
    int count;
    int cap;
};

static void Fail(const char *path, int line, const char *why)
{
    if (path != NULL)
    {
        fprintf(stderr, "ini: %s:%d: %s\n", path, line, why);
        return;
    }
    fprintf(stderr, "ini: %d: %s\n", line, why);
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

static char *Skip(char *s)
{
    while (*s == ' ' || *s == '\t')
    {
        s++;
    }
    return s;
}

static void TrimEnd(char *s)
{
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
    {
        end--;
    }
    *end = '\0';
}

static void CutComment(char *s)
{
    char *p = s;
    while (*p != '\0')
    {
        if ((*p == ' ' || *p == '\t') && (p[1] == ';' || p[1] == '#'))
        {
            *p = '\0';
            return;
        }
        p++;
    }
}

static int Unquote(char *s, char **after)
{
    char *r = s + 1;
    char *w = s;
    while (*r != '\0' && *r != '"')
    {
        if (*r == '\\')
        {
            char c;
            r++;
            if (*r == 'n')
            {
                c = '\n';
            }
            else if (*r == 't')
            {
                c = '\t';
            }
            else if (*r == '"' || *r == '\\')
            {
                c = *r;
            }
            else
            {
                return -1;
            }
            *w++ = c;
            r++;
            continue;
        }
        *w++ = *r++;
    }
    if (*r != '"')
    {
        return -1;
    }
    *w = '\0';
    *after = r + 1;
    return 0;
}

static int Push(BaseIni *ini, char *section, char *key, char *value)
{
    int i;
    IniItem *item;
    int cap;
    for (i = 0; i < ini->count; i++)
    {
        if (strcmp(ini->item[i].section, section) == 0 && strcmp(ini->item[i].key, key) == 0)
        {
            ini->item[i].value = value;
            return 0;
        }
    }
    if (ini->count == ini->cap)
    {
        cap = ini->cap == 0 ? 8 : ini->cap * 2;
        item = realloc(ini->item, (size_t)cap * sizeof(*item));
        if (item == NULL)
        {
            return -1;
        }
        ini->item = item;
        ini->cap = cap;
    }
    ini->item[ini->count].section = section;
    ini->item[ini->count].key = key;
    ini->item[ini->count].value = value;
    ini->count++;
    return 0;
}

static const char *Find(const BaseIni *ini, const char *section, const char *key)
{
    int i;
    for (i = 0; i < ini->count; i++)
    {
        if (strcmp(ini->item[i].section, section) == 0 && strcmp(ini->item[i].key, key) == 0)
        {
            return ini->item[i].value;
        }
    }
    return NULL;
}

static int Seen(const BaseIni *ini, int index)
{
    int i;
    for (i = 0; i < index; i++)
    {
        if (strcmp(ini->item[i].section, ini->item[index].section) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int TakeSection(const char *path, int line_no, char *s, char **section)
{
    char *end = strchr(s, ']');
    char *name;
    char *tail;
    if (end == NULL)
    {
        Fail(path, line_no, "unclosed section");
        return -1;
    }
    *end = '\0';
    name = Skip(s + 1);
    TrimEnd(name);
    if (name[0] == '\0' || strpbrk(name, "[] \t") != NULL)
    {
        Fail(path, line_no, "bad section");
        return -1;
    }
    tail = Skip(end + 1);
    if (tail[0] != '\0' && tail[0] != ';' && tail[0] != '#')
    {
        Fail(path, line_no, "trailing junk");
        return -1;
    }
    *section = name;
    return 0;
}

static int TakeLine(BaseIni *ini, const char *path, int line_no, char *line, char **section)
{
    char *s = Skip(line);
    char *eq;
    char *key;
    char *val;
    char *after;
    if (s[0] == '\0' || s[0] == ';' || s[0] == '#')
    {
        return 0;
    }
    if (s[0] == '[')
    {
        return TakeSection(path, line_no, s, section);
    }
    if (*section == NULL)
    {
        Fail(path, line_no, "key before section");
        return -1;
    }
    eq = strchr(s, '=');
    if (eq == NULL)
    {
        Fail(path, line_no, "missing '='");
        return -1;
    }
    *eq = '\0';
    key = Skip(s);
    TrimEnd(key);
    if (key[0] == '\0' || strpbrk(key, " \t[]") != NULL)
    {
        Fail(path, line_no, "bad key");
        return -1;
    }
    val = Skip(eq + 1);
    if (val[0] == '"')
    {
        if (Unquote(val, &after) != 0)
        {
            Fail(path, line_no, "bad quotes");
            return -1;
        }
        after = Skip(after);
        if (after[0] != '\0' && after[0] != ';' && after[0] != '#')
        {
            Fail(path, line_no, "trailing junk");
            return -1;
        }
    }
    else if (val[0] == ';' || val[0] == '#')
    {
        val[0] = '\0';
    }
    else
    {
        CutComment(val);
        TrimEnd(val);
    }
    if (Push(ini, *section, key, val) != 0)
    {
        Fail(path, line_no, "out of memory");
        return -1;
    }
    return 0;
}

static void StripBom(char *text)
{
    if ((unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
    {
        memmove(text, text + 3, strlen(text + 3) + 1);
    }
}

static void Normalize(char *text)
{
    char *r = text;
    char *w = text;
    while (*r != '\0')
    {
        if (*r == '\r')
        {
            *w++ = '\n';
            if (r[1] == '\n')
            {
                r++;
            }
        }
        else
        {
            *w++ = *r;
        }
        r++;
    }
    *w = '\0';
}

static int Parse(BaseIni *ini, const char *path)
{
    char *p;
    char *section = NULL;
    int line_no = 1;
    StripBom(ini->text);
    Normalize(ini->text);
    p = ini->text;
    while (*p != '\0')
    {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl != NULL)
        {
            *nl = '\0';
            p = nl + 1;
        }
        else
        {
            p += strlen(line);
        }
        if (TakeLine(ini, path, line_no, line, &section) != 0)
        {
            return -1;
        }
        if (nl != NULL)
        {
            line_no++;
        }
    }
    return 0;
}

static char *ReadFile(const char *path)
{
    FILE *fp;
    long size;
    char *text;
    size_t got;
    if (path == NULL || path[0] == '\0')
    {
        return NULL;
    }
    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "ini: cannot open %s\n", path);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        fprintf(stderr, "ini: cannot read %s\n", path);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || size > INI_MAX_BYTES || fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        fprintf(stderr, "ini: cannot read %s\n", path);
        return NULL;
    }
    text = malloc((size_t)size + 1);
    if (text == NULL)
    {
        fclose(fp);
        return NULL;
    }
    got = fread(text, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size)
    {
        free(text);
        fprintf(stderr, "ini: cannot read %s\n", path);
        return NULL;
    }
    if (memchr(text, '\0', (size_t)size) != NULL)
    {
        free(text);
        fprintf(stderr, "ini: %s: embedded nul\n", path);
        return NULL;
    }
    text[size] = '\0';
    return text;
}

static BaseIni *Own(char *text, const char *path)
{
    BaseIni *ini = calloc(1, sizeof(*ini));
    if (ini == NULL)
    {
        free(text);
        return NULL;
    }
    ini->text = text;
    if (Parse(ini, path) != 0)
    {
        base_ini_free(ini);
        return NULL;
    }
    return ini;
}

static int ParseInt(const char *s, int *out)
{
    const char *p = s;
    int neg = 0;
    int any = 0;
    long long v = 0;
    long long limit;
    if (s == NULL)
    {
        return 0;
    }
    if (*p == '+' || *p == '-')
    {
        neg = *p == '-';
        p++;
    }
    limit = neg ? 2147483648LL : (long long)INT_MAX;
    while (*p >= '0' && *p <= '9')
    {
        int digit = *p - '0';
        any = 1;
        if (v > (limit - digit) / 10)
        {
            return 0;
        }
        v = v * 10 + digit;
        p++;
    }
    if (!any || *p != '\0')
    {
        return 0;
    }
    *out = neg ? (int)(-v) : (int)v;
    return 1;
}

static int ParseBool(const char *s, int *out)
{
    if (Eq(s, "true") || Eq(s, "yes") || Eq(s, "on") || Eq(s, "1"))
    {
        *out = 1;
        return 1;
    }
    if (Eq(s, "false") || Eq(s, "no") || Eq(s, "off") || Eq(s, "0"))
    {
        *out = 0;
        return 1;
    }
    return 0;
}

static int ParseReal(const char *s, double *out)
{
    const char *p = s;
    int neg = 0;
    int digits = 0;
    int dot = 0;
    double v = 0.0;
    double place = 0.1;
    if (s == NULL)
    {
        return 0;
    }
    if (*p == '+' || *p == '-')
    {
        neg = *p == '-';
        p++;
    }
    while (*p != '\0')
    {
        if (*p == '.')
        {
            if (dot)
            {
                return 0;
            }
            dot = 1;
            p++;
            continue;
        }
        if (*p < '0' || *p > '9')
        {
            return 0;
        }
        digits = 1;
        if (!dot)
        {
            v = v * 10.0 + (*p - '0');
        }
        else
        {
            v += (*p - '0') * place;
            place *= 0.1;
        }
        if (v > 1.0e300)
        {
            return 0;
        }
        p++;
    }
    if (!digits)
    {
        return 0;
    }
    *out = neg ? -v : v;
    return 1;
}

BaseIni *base_ini_load(const char *path)
{
    char *text = ReadFile(path);
    if (text == NULL)
    {
        return NULL;
    }
    return Own(text, path);
}

BaseIni *base_ini_parse(const char *text)
{
    char *copy;
    size_t n;
    if (text == NULL)
    {
        return NULL;
    }
    n = strlen(text);
    if (n > INI_MAX_BYTES)
    {
        fprintf(stderr, "ini: text too large\n");
        return NULL;
    }
    copy = malloc(n + 1);
    if (copy == NULL)
    {
        return NULL;
    }
    memcpy(copy, text, n + 1);
    return Own(copy, NULL);
}

void base_ini_free(BaseIni *ini)
{
    if (ini == NULL)
    {
        return;
    }
    free(ini->text);
    free(ini->item);
    free(ini);
}

const char *base_ini_str(const BaseIni *ini, const char *section, const char *key, const char *def)
{
    const char *val;
    if (ini == NULL || section == NULL || key == NULL)
    {
        return def;
    }
    val = Find(ini, section, key);
    if (val == NULL)
    {
        return def;
    }
    return val;
}

int base_ini_has(const BaseIni *ini, const char *section, const char *key)
{
    if (ini == NULL || section == NULL || key == NULL)
    {
        return 0;
    }
    return Find(ini, section, key) != NULL;
}

BaseStatus base_ini_int(const BaseIni *ini, const char *section, const char *key, int def, int *out)
{
    const char *val;
    if (out == NULL)
    {
        return BASE_ERR;
    }
    *out = def;
    if (ini == NULL || section == NULL || key == NULL)
    {
        return BASE_ERR;
    }
    val = Find(ini, section, key);
    if (val == NULL)
    {
        return BASE_OK;
    }
    if (!ParseInt(val, out))
    {
        *out = def;
        return BASE_ERR;
    }
    return BASE_OK;
}

BaseStatus base_ini_bool(const BaseIni *ini, const char *section, const char *key, int def, int *out)
{
    const char *val;
    if (out == NULL)
    {
        return BASE_ERR;
    }
    *out = def;
    if (ini == NULL || section == NULL || key == NULL)
    {
        return BASE_ERR;
    }
    val = Find(ini, section, key);
    if (val == NULL)
    {
        return BASE_OK;
    }
    if (!ParseBool(val, out))
    {
        *out = def;
        return BASE_ERR;
    }
    return BASE_OK;
}

BaseStatus base_ini_real(const BaseIni *ini, const char *section, const char *key, double def, double *out)
{
    const char *val;
    if (out == NULL)
    {
        return BASE_ERR;
    }
    *out = def;
    if (ini == NULL || section == NULL || key == NULL)
    {
        return BASE_ERR;
    }
    val = Find(ini, section, key);
    if (val == NULL)
    {
        return BASE_OK;
    }
    if (!ParseReal(val, out))
    {
        *out = def;
        return BASE_ERR;
    }
    return BASE_OK;
}

BaseStatus base_ini_copy(const BaseIni *ini, const char *section, const char *key, const char *def,
                         char *dst, size_t dst_size)
{
    const char *val;
    size_t n;
    if (ini == NULL || section == NULL || key == NULL || dst == NULL || dst_size == 0)
    {
        return BASE_ERR;
    }
    val = base_ini_str(ini, section, key, def);
    if (val == NULL)
    {
        return BASE_ERR;
    }
    n = strlen(val);
    if (n >= dst_size)
    {
        return BASE_ERR;
    }
    memcpy(dst, val, n + 1);
    return BASE_OK;
}

int base_ini_sections(const BaseIni *ini, const char *prefix, const char **out, int max)
{
    int i;
    int n = 0;
    size_t plen;
    if (ini == NULL || prefix == NULL || max < 0)
    {
        return 0;
    }
    plen = strlen(prefix);
    for (i = 0; i < ini->count; i++)
    {
        const char *name = ini->item[i].section;
        if (strncmp(name, prefix, plen) != 0 || name[plen] == '\0' || Seen(ini, i))
        {
            continue;
        }
        if (out != NULL && n < max)
        {
            out[n] = name;
        }
        n++;
    }
    return n;
}
