# ESP32-S3 纯 AP 注入固件

车机（MG4 / SA8155P / Android 11）连上本固件开的热点后，应用商店的首页、详情页、
APK 下载全部由 ESP32 本地响应，**不需要任何上行网络**。

已在 macOS 侧用同一套逻辑验证：`0 次透传 / 1308 次拒绝` 下商店全链路走通并安装成功。

## 硬件

| | |
|---|---|
| 芯片 | ESP32-S3（QFN56 rev v0.2，已验证的那块） |
| PSRAM | 8MB embedded |
| Flash | 8MB（分区表按 8MB 配，更大也能用） |
| 接口 | USB-Serial/JTAG（`/dev/cu.usbmodem*`） |

## 结构

```
main/
├── main.c          NVS + AP(192.168.2.1) + 启动各服务
├── dns_server.c    UDP 53：4 个域名 → 本机；其余 NXDOMAIN
├── http80.c        TCP 80：任何请求回 204（车机联网探测）
├── tls443.c        TCP 443：按 SNI 分流 + HTTP 响应
├── rules.c         域名/路径匹配、JSON 模板、资源 blob
└── assets/
    ├── detail.json / banner.json / recommend.json / ordinary.json
    ├── mg4-adb.apk        485818 bytes
    ├── placeholder.png    70 bytes（1×1）
    ├── chain.pem          证书链 4 张
    └── leaf.key.pem       私钥
```

## 编译

推到 GitHub，Actions 自动构建（ESP-IDF v5.2 / esp32s3），产物是 `esp32-mg4-firmware`：

```
bootloader/bootloader.bin
partition_table/partition-table.bin
esp32-mg4.bin
```

本地编译也可以（需装 ESP-IDF 5.2）：

```bash
idf.py set-target esp32s3
idf.py build
```

## 烧录

```bash
./tools/flash.sh                                  # 默认 /dev/cu.usbmodem14301 + build/
./tools/flash.sh /dev/cu.usbmodem14301 ../build   # 手动指定
```

偏移：`0x0` bootloader、`0x8000` 分区表、`0x10000` app。

## 使用

1. 上电，串口（115200）应看到：
   ```
   AP 已启动: SSID=MG4-AP  IP=192.168.2.1
   DNS 服务启动 :53
   HTTP 204 服务启动 :80
   证书就绪，启动监听
   TLS 服务启动 :443
   ```
2. 车机 WLAN 里连 `MG4-AP`，密码 `mg4inject`
   - 若提示「无法访问互联网」，选**保持连接**
3. 等 10 秒左右，进应用商店 → 目标 app 详情页 → 安装

## 改配置

`main/main.c` 顶部：

```c
#define AP_SSID "MG4-AP"
#define AP_PASS "mg4inject"   // 至少 8 位
#define AP_CHAN 6
```

AP 的 IP 固定在 `192.168.2.1`（和 Mac 版一致，JSON 里的 URL 不用改）。

## 关键设计

| 点 | 做法 | 为什么 |
|---|---|---|
| IPv6 | 不启用（`CONFIG_LWIP_IPV6` 默认关，SoftAP 不发 RA） | 车机一旦拿到 IPv6 默认路由就绕过 v4 劫持 |
| 未命中的 SNI | 读到 ClientHello 后**直接关连接** | 无上行，不替车机转发；也不做 TLS 握手省 CPU |
| 未知 DNS | 回 NXDOMAIN | 比让客户端干等超时好 |
| APK | `EMBED_FILES` 进 app 分区，指针直接指向 flash | 不占 RAM，`send()` 时从 mmap 区读 |
| detail 的 appId | 从请求体提取后拼到模板末尾 | 写死会导致详情页空白 + `mDetailApp` 未初始化崩溃 |

## 已知限制

- 同时连接数受任务栈限制（每个 TLS 连接 12KB 栈），车机通常只开 2-4 个
- 证书只对未更新信任库的旧车机有效（该中间证书 2026-10 已被吊销）
- 后台应用（语音、百度地图）会持续重试并被关闭，属正常现象，不影响商店
