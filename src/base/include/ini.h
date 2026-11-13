/**
 * @file    ini.h
 * @brief   只读 ini。整份读入内存，段和键直接指向这份文本。
 *
 * 段名、键、值区分大小写。以 ; 或 # 起行的是注释。
 * 引号里保留空格、等号和分号，转义只认 \\ \" \n \t。
 * 没加引号时，空白后面的 ; 或 # 才是行内注释。
 * 同一段里后写的键覆盖先写的。键必须出现在某一段下面。
 * 取值指针保持到 base_ini_free。加载之后只读，多线程可以同时读。
 */
#ifndef BASE_INI_H
#define BASE_INI_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct BaseIni BaseIni;

/* 打不开、超过 1MB、或某一行不合法时返回空。 */
BaseIni *base_ini_load(const char *path);

/* text 以 \0 结束。解析的是副本。 */
BaseIni *base_ini_parse(const char *text);

void base_ini_free(BaseIni *ini);

/* 没有该键时返回 def。def 可以是空。 */
const char *base_ini_str(const BaseIni *ini, const char *section, const char *key, const char *def);

/* 有键返回 1，值为空串也算有。 */
int base_ini_has(const BaseIni *ini, const char *section, const char *key);

/*
 * 键缺失时 *out 取 def，返回 BASE_OK。
 * 键在但写错时 *out 取 def，返回 BASE_ERR。
 * 布尔接受 true/false、yes/no、on/off、1/0，不区分大小写。
 */
BaseStatus base_ini_int(const BaseIni *ini, const char *section, const char *key, int def, int *out);
BaseStatus base_ini_bool(const BaseIni *ini, const char *section, const char *key, int def, int *out);
BaseStatus base_ini_real(const BaseIni *ini, const char *section, const char *key, double def, double *out);

/* 拷进 dst。装不下或没有可拷的字符串时返回 BASE_ERR，dst 不动。 */
BaseStatus base_ini_copy(const BaseIni *ini, const char *section, const char *key, const char *def,
                         char *dst, size_t dst_size);

/*
 * 段名以 prefix 开头，并且后面还有字符。prefix 用 "camera." 这种带点的前缀。
 * prefix 为空串时列出全部段。顺序是文件里第一次出现的顺序。
 * out 可空。返回的个数可以大于 max，多出来的不写。指针指向 ini 内部。
 */
int base_ini_sections(const BaseIni *ini, const char *prefix, const char **out, int max);

#ifdef __cplusplus
}
#endif

#endif
