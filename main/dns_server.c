// UDP 53：劫持商店域名，其余回 NXDOMAIN（无上行，快速失败优于超时）
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "rules.h"

static const char *TAG = "dns";

#define DNS_PORT 53
#define DNS_MAX 512

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

// 把 question 段（含末尾 0）的原始字节解析成小写域名
static int parse_qname(const uint8_t *buf, int len, int off, char *out, size_t outsz) {
    size_t w = 0;
    while (off < len) {
        uint8_t l = buf[off++];
        if (l == 0) {
            if (w < outsz) {
                out[w] = 0;
            }
            return off;  // 下一个字段的偏移
        }
        if (l & 0xC0) {  // 压缩指针，查询里不该出现
            return -1;
        }
        if (off + l > len || w + l + 2 >= outsz) {
            return -1;
        }
        if (w) {
            out[w++] = '.';
        }
        for (int i = 0; i < l; i++) {
            char c = (char)buf[off++];
            out[w++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
        }
    }
    return -1;
}

static void dns_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket 失败");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind 53 失败");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "DNS 服务启动 :53");

    uint8_t q[DNS_MAX];
    uint8_t r[DNS_MAX];
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);

    for (;;) {
        int n = recvfrom(sock, q, sizeof(q), 0, (struct sockaddr *)&peer, &plen);
        if (n < 12) {
            continue;
        }
        char name[128];
        int qend = parse_qname(q, n, 12, name, sizeof(name));
        if (qend < 0 || qend + 4 > n) {
            continue;
        }
        uint16_t qtype = (uint16_t)((q[qend] << 8) | q[qend + 1]);
        int qlen = qend + 4;  // question 段总长

        memcpy(r, q, qlen);
        r[2] = 0x81;  // QR=1 RD=1
        r[3] = 0x80;  // RA=1

        int ans = 0;
        if (rules_is_store_host(name)) {
            if (qtype == 1) {
                // A → AP 自己的地址
                esp_netif_ip_info_t ip = {0};
                esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
                if (ap) {
                    esp_netif_get_ip_info(ap, &ip);
                }
                uint32_t a = ip.ip.addr ? ip.ip.addr : htonl(0xC0A80201);  // 兜底 192.168.2.1
                uint8_t *p = r + qlen;
                p[0] = 0xC0; p[1] = 0x0C;              // 指向 question 的 name
                put16(p + 2, 1);                        // TYPE A
                put16(p + 4, 1);                        // CLASS IN
                p[6] = 0; p[7] = 0; p[8] = 0; p[9] = 60; // TTL 60
                put16(p + 10, 4);
                memcpy(p + 12, &a, 4);
                ans = 1;
                ESP_LOGI(TAG, "[HIJACK] %s A -> 本地 (%s)", name, inet_ntoa(*(struct in_addr *)&a));
            } else if (qtype == 28) {
                // AAAA → NOERROR 空应答（热点无 IPv6）
                ans = 0;
                ESP_LOGI(TAG, "[HIJACK] %s AAAA -> 空应答", name);
            }
        } else {
            r[3] = 0x83;  // NXDOMAIN，快速失败
            ESP_LOGD(TAG, "[NX] %s", name);
        }

        put16(r + 6, (uint16_t)ans);
        put16(r + 8, 0);
        put16(r + 10, 0);

        int rlen = qlen + (ans ? 16 : 0);
        sendto(sock, r, rlen, 0, (struct sockaddr *)&peer, plen);
    }
}

void dns_server_start(void) {
    xTaskCreate(dns_task, "dns", 4096, NULL, 6, NULL);
}
