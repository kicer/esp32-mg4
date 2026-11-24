// ESP32-S3 纯 AP 注入固件
// AP(192.168.2.1) + DNS 劫持 + :80 回 204 + :443 本地响应，无任何上行
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "nvs_flash.h"

#include "rules.h"

static const char *TAG = "main";

#define AP_SSID "MG4-AP"
#define AP_PASS "mg4inject"  // 至少 8 位
#define AP_CHAN 6

void dns_server_start(void);
void http80_start(void);
void tls443_start(void);

static void wifi_init_ap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap = esp_netif_create_default_wifi_ap();

    // 固定 192.168.2.1，与 Mac 环境保持一致
    esp_netif_dhcps_stop(ap);
    esp_netif_ip_info_t ip = {0};
    IP4_ADDR(&ip.ip, 192, 168, 2, 1);
    IP4_ADDR(&ip.gw, 192, 168, 2, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap, &ip));
    esp_netif_dhcps_start(ap);

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.ap.ssid, AP_SSID, sizeof(cfg.ap.ssid));
    strncpy((char *)cfg.ap.password, AP_PASS, sizeof(cfg.ap.password) - 1);
    cfg.ap.ssid_len = strlen(AP_SSID);
    cfg.ap.channel = AP_CHAN;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP 已启动: SSID=%s  IP=%s", AP_SSID, "192.168.2.1");
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    rules_init();
    ESP_LOGI(TAG, "==== MG4 注入固件启动 ====");
    wifi_init_ap();

    dns_server_start();
    http80_start();
    tls443_start();

    ESP_LOGI(TAG, "全部服务就绪：DNS:53  HTTP:80  TLS:443  （无上行）");
}
