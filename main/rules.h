// 规则与资源：域名匹配、路径匹配、JSON 模板
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t len;
} blob_t;

typedef enum {
    RULE_NONE = 0,
    RULE_DETAIL,
    RULE_BANNER,
    RULE_RECOMMEND,
    RULE_ORDINARY,
    RULE_APK,
    RULE_PNG,
} rule_id_t;

// 外部符号地址相减不是编译期常量，只能运行时赋值（见 rules_init）
extern blob_t g_detail_tpl;
extern blob_t g_banner;
extern blob_t g_recommend;
extern blob_t g_ordinary;
extern blob_t g_apk;
extern blob_t g_png;

void rules_init(void);
extern const char *g_chain_pem;
extern const char *g_key_pem;

// SNI / Host 是否为商店域名（命中才做 TLS 握手，否则直接关连接）
bool rules_is_store_host(const char *host);

// 路径 → 规则
rule_id_t rules_match_path(const char *path);

// detail 响应：模板末尾追加 appId / vin（与原 Python 实现一致）
// 返回写入长度，失败返回 -1
int rules_build_detail(const char *appid, const char *vin, char *out, size_t outsz);

// 从 JSON 文本里取字符串字段值，成功返回 0
int rules_json_get_str(const char *json, const char *key, char *out, size_t outsz);
