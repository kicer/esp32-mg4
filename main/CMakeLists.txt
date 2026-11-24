idf_component_register(
    SRCS "main.c"
         "rules.c"
         "dns_server.c"
         "http80.c"
         "tls443.c"
    INCLUDE_DIRS "."
    EMBED_FILES "assets/mg4-adb.apk"
                "assets/placeholder.png"
    EMBED_TXTFILES "assets/chain.pem"
                   "assets/leaf.key.pem"
                   "assets/detail.json"
                   "assets/banner.json"
                   "assets/recommend.json"
                   "assets/ordinary.json"
    REQUIRES esp_wifi nvs_flash esp_timer mbedtls lwip esp_netif
)
