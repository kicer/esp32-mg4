// TCP 443：按 SNI 分流。命中的走本地 TLS 响应，未命中的直接关连接（等价 ESP32 无上行）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "rules.h"

static const char *TAG = "tls443";

static mbedtls_ssl_config s_conf;
static mbedtls_x509_crt s_crt;
static mbedtls_pk_context s_key;
static mbedtls_ctr_drbg_context s_drbg;
static mbedtls_entropy_context s_entropy;

// newlib 不一定提供 strcasestr
static char *find_ci(char *hay, const char *needle) {
    size_t nl = strlen(needle);
    if (!nl) {
        return hay;
    }
    for (char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, nl) == 0) {
            return p;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------- SNI 解析
// 从 TLS ClientHello 里取 server_name。不消费数据（MSG_PEEK）
static int peek_sni(int fd, char *out, size_t outsz) {
    uint8_t buf[2048];
    int n = recv(fd, buf, sizeof(buf), MSG_PEEK);
    if (n < 5 + 4 + 2 + 32 + 1) {
        return -1;
    }
    int p = 5;  // 跳过 record header
    if (buf[p] != 0x01) {
        return -1;  // 不是 ClientHello
    }
    p += 4;  // handshake type + length(3)
    p += 2 + 32;  // client_version + random
    int sid = buf[p];
    p += 1 + sid;
    if (p + 2 > n) {
        return -1;
    }
    int cs = (buf[p] << 8) | buf[p + 1];
    p += 2 + cs;
    if (p + 1 > n) {
        return -1;
    }
    int cm = buf[p];
    p += 1 + cm;
    if (p + 2 > n) {
        return -1;
    }
    int ext_total = (buf[p] << 8) | buf[p + 1];
    p += 2;
    int end = p + ext_total;
    if (end > n) {
        end = n;
    }
    while (p + 4 <= end) {
        int type = (buf[p] << 8) | buf[p + 1];
        int len = (buf[p + 2] << 8) | buf[p + 3];
        p += 4;
        if (p + len > end) {
            break;
        }
        if (type == 0x0000 && len >= 5) {  // server_name
            int list_len = (buf[p] << 8) | buf[p + 1];
            int q = p + 2;
            int qend = p + 2 + list_len;
            if (qend > p + len) {
                qend = p + len;
            }
            while (q + 3 <= qend) {
                int ntype = buf[q];
                int nlen = (buf[q + 1] << 8) | buf[q + 2];
                q += 3;
                if (q + nlen > qend) {
                    break;
                }
                if (ntype == 0 && (size_t)nlen < outsz) {
                    memcpy(out, buf + q, nlen);
                    out[nlen] = 0;
                    return 0;
                }
                q += nlen;
            }
        }
        p += len;
    }
    return -1;
}

// ---------------------------------------------------------------- BIO
static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    int ret = send(fd, buf, len, 0);
    if (ret < 0) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    int ret = recv(fd, buf, len, 0);
    if (ret == 0) {
        return MBEDTLS_ERR_NET_CONN_RESET;
    }
    if (ret < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}

// ---------------------------------------------------------------- HTTP 响应
static void send_all(mbedtls_ssl_context *ssl, const void *data, size_t len) {
    const uint8_t *p = data;
    while (len) {
        int n = mbedtls_ssl_write(ssl, p, len > 4096 ? 4096 : len);
        if (n <= 0) {
            return;
        }
        p += n;
        len -= (size_t)n;
    }
}

static void send_json(mbedtls_ssl_context *ssl, const void *body, size_t len) {
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n\r\n",
                     (unsigned)len);
    send_all(ssl, head, (size_t)n);
    send_all(ssl, body, len);
}

static void send_blob(mbedtls_ssl_context *ssl, const blob_t *b, const char *ctype,
                      long start, long end) {
    char head[320];
    int n;
    if (start >= 0) {
        if (end < 0 || end >= (long)b->len) {
            end = (long)b->len - 1;
        }
        long part = end - start + 1;
        n = snprintf(head, sizeof(head),
                     "HTTP/1.1 206 Partial Content\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Range: bytes %ld-%ld/%u\r\n"
                     "Content-Length: %ld\r\n"
                     "Connection: close\r\n\r\n",
                     ctype, start, end, (unsigned)b->len, part);
        send_all(ssl, head, (size_t)n);
        send_all(ssl, b->data + start, (size_t)part);
    } else {
        n = snprintf(head, sizeof(head),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n\r\n",
                     ctype, (unsigned)b->len);
        send_all(ssl, head, (size_t)n);
        send_all(ssl, b->data, b->len);
    }
}

// ---------------------------------------------------------------- 单连接
static void conn_task(void *arg) {
    int fd = (int)(intptr_t)arg;
    char sni[128] = {0};
    char host[128] = {0};
    char path[320] = {0};
    char method[8] = {0};
    char rng[48] = {0};
    char body[512] = {0};
    int body_len = 0;

    if (peek_sni(fd, sni, sizeof(sni)) != 0) {
        ESP_LOGW(TAG, "无 SNI, 关闭");
        goto done;
    }
    if (!rules_is_store_host(sni)) {
        // 未命中：直接关（ESP32 无上行，不替车机转发）
        ESP_LOGD(TAG, "拒绝透传 %s", sni);
        goto done;
    }

    mbedtls_ssl_context *ssl = calloc(1, sizeof(mbedtls_ssl_context));
    if (!ssl) {
        goto done;
    }
    mbedtls_ssl_init(ssl);
    if (mbedtls_ssl_setup(ssl, &s_conf) != 0) {
        mbedtls_ssl_free(ssl);
        free(ssl);
        goto done;
    }
    mbedtls_ssl_set_bio(ssl, &fd, bio_send, bio_recv, NULL);

    int ret = mbedtls_ssl_handshake(ssl);
    if (ret != 0) {
        ESP_LOGW(TAG, "TLS 握手失败 %s: -0x%04X", sni, -ret);
        mbedtls_ssl_free(ssl);
        free(ssl);
        goto done;
    }

    // 读 HTTP 请求头
    char req[2048];
    int got = 0, hdr_end = -1;
    while (got < (int)sizeof(req) - 1) {
        int n = mbedtls_ssl_read(ssl, (unsigned char *)req + got, sizeof(req) - 1 - got);
        if (n <= 0) {
            break;
        }
        got += n;
        req[got] = 0;
        char *e = strstr(req, "\r\n\r\n");
        if (e) {
            hdr_end = (int)(e - req) + 4;
            break;
        }
    }
    if (hdr_end < 0) {
        mbedtls_ssl_free(ssl);
        free(ssl);
        goto done;
    }

    // 请求行
    sscanf(req, "%7s %319s", method, path);
    // Host
    char *h = find_ci(req, "\r\nHost:");
    if (h) {
        h += 7;
        while (*h == ' ') h++;
        int i = 0;
        while (*h && *h != '\r' && *h != '\n' && *h != ':' && i < (int)sizeof(host) - 1) {
            host[i++] = *h++;
        }
        host[i] = 0;
    }
    // Range: bytes=0-0
    char *rg = find_ci(req, "\r\nRange:");
    if (rg) {
        rg += 8;
        while (*rg == ' ') rg++;
        int i = 0;
        while (*rg && *rg != '\r' && *rg != '\n' && i < (int)sizeof(rng) - 1) {
            rng[i++] = *rg++;
        }
        rng[i] = 0;
    }
    // Content-Length + body
    char *cl = find_ci(req, "\r\nContent-Length:");
    if (cl) {
        int want = atoi(cl + 17);
        body_len = got - hdr_end;
        if (body_len > (int)sizeof(body) - 1) {
            body_len = (int)sizeof(body) - 1;
        }
        if (body_len > 0) {
            memcpy(body, req + hdr_end, (size_t)body_len);
            body[body_len] = 0;
        }
        // 不够就继续读
        while (want > body_len && body_len < (int)sizeof(body) - 1) {
            int n = mbedtls_ssl_read(ssl, (unsigned char *)body + body_len,
                                     sizeof(body) - 1 - body_len);
            if (n <= 0) {
                break;
            }
            body_len += n;
            body[body_len] = 0;
        }
    }

    rule_id_t r = rules_match_path(path);
    long rs = -1, re = -1;
    if (rng[0] && sscanf(rng, "bytes=%ld-%ld", &rs, &re) < 1) {
        rs = -1;
    }
    ESP_LOGI(TAG, "%s %s%s (sni=%s, rule=%d, range=%s)",
             method, host[0] ? host : "", path, sni, (int)r, rng[0] ? rng : "-");

    switch (r) {
        case RULE_DETAIL: {
            char appid[128] = {0}, vin[64] = {0};
            rules_json_get_str(body, "appId", appid, sizeof(appid));
            rules_json_get_str(body, "vin", vin, sizeof(vin));
            char *out = malloc(4096);
            if (out) {
                int n = rules_build_detail(appid, vin, out, 4096);
                if (n > 0) {
                    send_json(ssl, out, (size_t)n);
                    ESP_LOGI(TAG, "[patch] detail %d bytes (appId=%s)", n, appid);
                }
                free(out);
            }
            break;
        }
        case RULE_BANNER:
            send_json(ssl, g_banner.data, g_banner.len);
            ESP_LOGI(TAG, "[patch] banner %u bytes", (unsigned)g_banner.len);
            break;
        case RULE_RECOMMEND:
            send_json(ssl, g_recommend.data, g_recommend.len);
            ESP_LOGI(TAG, "[patch] recommend %u bytes", (unsigned)g_recommend.len);
            break;
        case RULE_ORDINARY:
            send_json(ssl, g_ordinary.data, g_ordinary.len);
            ESP_LOGI(TAG, "[patch] ordinary %u bytes", (unsigned)g_ordinary.len);
            break;
        case RULE_APK:
            send_blob(ssl, &g_apk, "application/vnd.android.package-archive", rs, re);
            ESP_LOGI(TAG, "[serve] APK %u bytes (range %ld-%ld)",
                     (unsigned)g_apk.len, rs, re);
            break;
        case RULE_PNG:
            send_blob(ssl, &g_png, "image/png", rs, re);
            break;
        default:
            // 商店域名但路径不认识：回 404 而不是挂起
            static const char k404[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(ssl, k404, sizeof(k404) - 1);
            break;
    }

    mbedtls_ssl_close_notify(ssl);
    mbedtls_ssl_free(ssl);
    free(ssl);

done:
    shutdown(fd, 0);
    close(fd);
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------- 监听
static void listen_task(void *arg) {
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        ESP_LOGE(TAG, "socket 失败");
        vTaskDelete(NULL);
        return;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(443);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 8) < 0) {
        ESP_LOGE(TAG, "bind/listen 443 失败");
        close(s);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "TLS 服务启动 :443");

    for (;;) {
        struct sockaddr_in peer;
        socklen_t pl = sizeof(peer);
        int fd = accept(s, (struct sockaddr *)&peer, &pl);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        // 栈要够 mbedTLS 握手（8KB）
        if (xTaskCreate(conn_task, "tls", 12288, (void *)(intptr_t)fd, 5, NULL) != pdPASS) {
            close(fd);
        }
    }
}

void tls443_start(void) {
    mbedtls_ssl_config_init(&s_conf);
    mbedtls_x509_crt_init(&s_crt);
    mbedtls_pk_init(&s_key);
    mbedtls_ctr_drbg_init(&s_drbg);
    mbedtls_entropy_init(&s_entropy);

    int ret = mbedtls_x509_crt_parse(&s_crt, (const unsigned char *)g_chain_pem,
                                     strlen(g_chain_pem) + 1);
    if (ret < 0) {
        ESP_LOGE(TAG, "证书解析失败 -0x%04X", -ret);
        return;
    }
    ret = mbedtls_pk_parse_key(&s_key, (const unsigned char *)g_key_pem,
                               strlen(g_key_pem) + 1, NULL, 0);
    if (ret < 0) {
        ESP_LOGE(TAG, "私钥解析失败 -0x%04X", -ret);
        return;
    }
    ret = mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy, NULL, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "drbg 失败 -0x%04X", -ret);
        return;
    }
    ret = mbedtls_ssl_config_defaults(&s_conf, MBEDTLS_SSL_IS_SERVER,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        ESP_LOGE(TAG, "ssl_config 失败 -0x%04X", -ret);
        return;
    }
    mbedtls_ssl_conf_rng(&s_conf, mbedtls_ctr_drbg_random, &s_drbg);
    mbedtls_ssl_conf_authmode(&s_conf, MBEDTLS_SSL_VERIFY_NONE);
    ret = mbedtls_ssl_conf_own_cert(&s_conf, &s_crt, &s_key);
    if (ret != 0) {
        ESP_LOGE(TAG, "配证书失败 -0x%04X", -ret);
        return;
    }
    ESP_LOGI(TAG, "证书就绪，启动监听");

    xTaskCreate(listen_task, "tls443", 4096, NULL, 6, NULL);
}
