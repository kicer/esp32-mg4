// TCP 80：任何请求都回 204 —— 车机联网探测走 http://204.z-onesoft.com/
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "http80";

static const char k204[] =
    "HTTP/1.1 204 No Content\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";

static void conn_task(void *arg) {
    int fd = (int)(intptr_t)arg;
    char buf[512];
    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    // 请求头不读完也无所谓，客户端只关心 204
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    recv(fd, buf, sizeof(buf), 0);
    send(fd, k204, sizeof(k204) - 1, 0);
    shutdown(fd, 0);
    close(fd);
    vTaskDelete(NULL);
}

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
    a.sin_port = htons(80);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 8) < 0) {
        ESP_LOGE(TAG, "bind/listen 80 失败");
        close(s);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "HTTP 204 服务启动 :80");

    for (;;) {
        struct sockaddr_in peer;
        socklen_t pl = sizeof(peer);
        int fd = accept(s, (struct sockaddr *)&peer, &pl);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        ESP_LOGI(TAG, "204 -> %s", inet_ntoa(peer.sin_addr));
        if (xTaskCreate(conn_task, "h80", 3072, (void *)(intptr_t)fd, 5, NULL) != pdPASS) {
            close(fd);
        }
    }
}

void http80_start(void) {
    xTaskCreate(listen_task, "h80s", 4096, NULL, 6, NULL);
}
