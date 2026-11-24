#include "rules.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

// EMBED_FILES / EMBED_TXTFILES 生成在 assets/xxx 下的符号
extern const uint8_t _binary_mg4_adb_apk_start[] asm("_binary_mg4_adb_apk_start");
extern const uint8_t _binary_mg4_adb_apk_end[] asm("_binary_mg4_adb_apk_end");
extern const uint8_t _binary_placeholder_png_start[] asm("_binary_placeholder_png_start");
extern const uint8_t _binary_placeholder_png_end[] asm("_binary_placeholder_png_end");
extern const char _binary_chain_pem_start[] asm("_binary_chain_pem_start");
extern const char _binary_leaf_key_pem_start[] asm("_binary_leaf_key_pem_start");
extern const uint8_t _binary_detail_json_start[] asm("_binary_detail_json_start");
extern const uint8_t _binary_detail_json_end[] asm("_binary_detail_json_end");
extern const uint8_t _binary_banner_json_start[] asm("_binary_banner_json_start");
extern const uint8_t _binary_banner_json_end[] asm("_binary_banner_json_end");
extern const uint8_t _binary_recommend_json_start[] asm("_binary_recommend_json_start");
extern const uint8_t _binary_recommend_json_end[] asm("_binary_recommend_json_end");
extern const uint8_t _binary_ordinary_json_start[] asm("_binary_ordinary_json_start");
extern const uint8_t _binary_ordinary_json_end[] asm("_binary_ordinary_json_end");

// EMBED_TXTFILES 末尾会自动补一个 \0，文本长度要减掉
#define SET_BLOB_TXT(b, s, e) do { \
    (b).data = (const uint8_t *)(s); \
    (b).len = (size_t)((e) - (s)) - 1; \
} while (0)
#define SET_BLOB(b, s, e) do { \
    (b).data = (const uint8_t *)(s); \
    (b).len = (size_t)((e) - (s)); \
} while (0)

blob_t g_detail_tpl;
blob_t g_banner;
blob_t g_recommend;
blob_t g_ordinary;
blob_t g_apk;
blob_t g_png;

// 指针型资源地址是链接期常量，可以直接静态初始化
const char *g_chain_pem = _binary_chain_pem_start;
const char *g_key_pem = _binary_leaf_key_pem_start;

void rules_init(void) {
    SET_BLOB_TXT(g_detail_tpl, _binary_detail_json_start, _binary_detail_json_end);
    SET_BLOB_TXT(g_banner, _binary_banner_json_start, _binary_banner_json_end);
    SET_BLOB_TXT(g_recommend, _binary_recommend_json_start, _binary_recommend_json_end);
    SET_BLOB_TXT(g_ordinary, _binary_ordinary_json_start, _binary_ordinary_json_end);
    SET_BLOB(g_apk, _binary_mg4_adb_apk_start, _binary_mg4_adb_apk_end);
    SET_BLOB(g_png, _binary_placeholder_png_start, _binary_placeholder_png_end);
}

// 需要劫持 / 本地响应的域名
static const char *const k_hosts[] = {
    "204.z-onesoft.com",
    "prod-r-platform.z-onesoft.com",
    "prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com",
    "prod-dp-public-es33-tos.z-onesoft.com",
};

bool rules_is_store_host(const char *host) {
    if (!host) {
        return false;
    }
    for (size_t i = 0; i < sizeof(k_hosts) / sizeof(k_hosts[0]); i++) {
        if (strcasecmp(host, k_hosts[i]) == 0) {
            return true;
        }
    }
    return false;
}

rule_id_t rules_match_path(const char *path) {
    if (!path) {
        return RULE_NONE;
    }
    if (strncmp(path, "/eco/ippf/appMarket/vehicle/app/detail", 39) == 0) {
        return RULE_DETAIL;
    }
    if (strncmp(path, "/eco/ippf/appMarket/vehicle/banner/list", 40) == 0) {
        return RULE_BANNER;
    }
    if (strncmp(path, "/eco/ippf/appMarket/vehicle/category/recommend/list", 52) == 0) {
        return RULE_RECOMMEND;
    }
    if (strncmp(path, "/eco/ippf/appMarket/vehicle/category/ordinary/list", 51) == 0) {
        return RULE_ORDINARY;
    }
    if (strncmp(path, "/ecology/", 9) == 0) {
        size_t n = strlen(path);
        if (n > 4 && strcasecmp(path + n - 4, ".apk") == 0) {
            return RULE_APK;
        }
        if (n > 4 && strcasecmp(path + n - 4, ".png") == 0) {
            return RULE_PNG;
        }
    }
    return RULE_NONE;
}

int rules_build_detail(const char *appid, const char *vin, char *out, size_t outsz) {
    // 模板里已埋了 "appId":"__APPID__","vin":"__VIN__"，逐段替换
    const char *p = (const char *)g_detail_tpl.data;
    const char *end = p + g_detail_tpl.len;
    char *w = out;
    char *stop = out + outsz - 1;

    while (p < end && w < stop) {
        if ((size_t)(end - p) >= 9 && memcmp(p, "__APPID__", 9) == 0) {
            const char *v = appid ? appid : "";
            size_t n = strlen(v);
            if (w + n >= stop) {
                n = (size_t)(stop - w);
            }
            memcpy(w, v, n);
            w += n;
            p += 9;
        } else if ((size_t)(end - p) >= 7 && memcmp(p, "__VIN__", 7) == 0) {
            const char *v = vin ? vin : "";
            size_t n = strlen(v);
            if (w + n >= stop) {
                n = (size_t)(stop - w);
            }
            memcpy(w, v, n);
            w += n;
            p += 7;
        } else {
            *w++ = *p++;
        }
    }
    *w = 0;
    return (int)(w - out);
}

int rules_json_get_str(const char *json, const char *key, char *out, size_t outsz) {
    if (!json || !key || !out || outsz == 0) {
        return -1;
    }
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return -1;
    }
    p += strlen(pat);
    const char *e = strchr(p, '"');
    if (!e) {
        return -1;
    }
    size_t n = (size_t)(e - p);
    if (n >= outsz) {
        n = outsz - 1;
    }
    memcpy(out, p, n);
    out[n] = 0;
    return 0;
}
