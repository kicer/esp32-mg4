# esp32-mg4

MG4 车机应用商店的本地注入固件 —— **ESP32-S3 纯 AP 实现**。

车机连上这个 AP 之后，应用商店的首页、详情页、APK 下载全部由 ESP32 本地应答，
**不需要任何上行链路**（AP 自己不联网，也不替车机转发）。

---

## 提供的服务

| 端口 | 协议 | 作用 |
|---|---|---|
| 53 | UDP | 劫持 4 个商店域名 → `192.168.2.1`，其余回 NXDOMAIN，AAAA 一律空应答 |
| 80 | TCP | 任何请求回 `204`（车机联网探测 `http://204.z-onesoft.com/`） |
| 443 | TCP | 按 SNI 分流：命中的走本地 TLS，未命中的读完 ClientHello 直接关闭 |

本地应答的四个接口：

```
POST /eco/ippf/appMarket/vehicle/banner/list                 → banner.json
POST /eco/ippf/appMarket/vehicle/category/recommend/list     → recommend.json
POST /eco/ippf/appMarket/vehicle/category/ordinary/list      → ordinary.json
POST /eco/ippf/appMarket/vehicle/app/detail                  → detail.json（appId/vin 从请求体回填）
GET  /ecology/*.apk                                          → 内置 APK，支持 Range
GET  /ecology/*.png                                          → 1×1 占位图
```

---

## 硬件

- **ESP32-S3**，8MB flash + Octal PSRAM
- 实测板：`ESP32-S3 (QFN56) rev v0.2`，MAC `cc:8d:a2:ee:9f:e4`

AP 参数在 `main/main.c` 顶部：

```c
#define AP_SSID "MG4-AP"
#define AP_PASS "mg4inject"     // 至少 8 位
```

IP 固定 `192.168.2.1`（与车机原有配置一致，见 `wifi_init_ap()`）。

---

## 目录

```
main/
  main.c         AP 启动、服务编排
  dns_server.c   UDP 53
  http80.c       TCP 80 → 204
  tls443.c       TCP 443：SNI 分流 + HTTP 响应 + 并发闸
  rules.c/.h     域名/路径匹配、JSON 模板、资源 blob
  assets/        4 个 JSON、APK、占位图、证书链与私钥
tools/
  flash.sh         烧录（优先单文件镜像）
  merge_bins.py    三个 bin 合成一个可直接烧到 0x0 的镜像
  selftest.sh      连上 AP 后的本地自检（13 组 / 19 项）
  serial-log.sh    抓串口 N 秒
  serial-watch.sh  后台持续记录串口到文件
  check-certs.sh   校验证书链/有效期/SAN/私钥配对
  gen-certs.sh     生成自签证书（仅供本机 curl 调试，车机不认）
  make-assets.py   校验或重新生成 assets（含 detail 占位符注入）
```

---

## 编译

**CI（推荐）**：推送到仓库即可，`.github/workflows/build.yml` 会用 `espressif/idf:v5.2`
编译、合并、打 checksum，并自动建一个 `build-N` 的 release，附件公开可下载。

**本地**（需自装 ESP-IDF v5.2）：

```bash
idf.py set-target esp32s3
idf.py build
```

---

## 烧录

产物里 **`esp32-mg4-full.bin` 是单文件镜像**，一次烧到 `0x0` 就行：

```bash
bash tools/flash.sh /dev/cu.usbmodem14301 dist
```

等价的手工命令：

```bash
esptool -c esp32s3 -p /dev/cu.usbmodem14301 -b 460800 \
  write-flash --flash-mode dio --flash-freq 80m --flash-size 8MB \
  0x0 esp32-mg4-full.bin
```

`tools/merge_bins.py` 做的就是这个合并（布局 `0x0` bootloader / `0x8000` 分区表 /
`0x10000` app，空隙填 `0xFF`）。分段烧录的方式也保留在 `flash.sh` 里，作为兜底。

烧完 ESP32 自动重启，串口应该看到：

```
I (966) main: AP 已启动: SSID=MG4-AP  IP=192.168.2.1
I (966) dns: DNS 服务启动 :53
I (966) http80: HTTP 204 服务启动 :80
I (976) tls443: 证书就绪，启动监听
I (976) tls443: TLS 服务启动 :443
I (976) main: 全部服务就绪：DNS:53  HTTP:80  TLS:443  （无上行）
```

---

## 验证

车机（或电脑）连上 `MG4-AP` 后跑：

```bash
bash tools/selftest.sh
```

覆盖 19 项：链路、DNS 劫持、AAAA 抑制、80 回 204、TLS 证书、三个列表接口、
detail 的 appId/vin 回填、APK 大小与 sha256、Range 206、占位图、未命中快速关闭。

参考结果：

```
通过 19 项，失败 0 项
全部通过 —— 固件行为与设计一致
```

报告同时写到 `tools/selftest-report.txt`。

### 看串口

```bash
bash tools/serial-log.sh 30     # 抓 30 秒，期间按一下 RESET
bash tools/serial-watch.sh      # 后台持续记录到 logs/esp32-serial.log
```

> 脚本用 `os.open` 直接读，不碰 DTR/RTS。用 pyserial 默认打开会把 DTR 拉低，
> 板子上 DTR 接 EN 的话等于一直按住复位，读到的永远是 0 字节。

日志级别：命中请求是 `INFO`（能看到每个接口和字节数），未命中和并发拒绝是 `DEBUG`
（默认不输出，不会刷屏）。

---

## 证书

`main/assets/` 里的 `chain.pem`（4 张）和 `leaf.key.pem` 是一套 eMudhra 签发的证书，
SAN 覆盖：

```
prod-r-platform.z-onesoft.com
*.z-onesoft.com                     ← 覆盖 CDN 和 204 探测域名
prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com
```

**这套证书只对信任库较旧的车机有效**。签发它的中间证书已在 2026-10 被吊销，
车机若更新过信任库，握手会被拒绝。

检查当前证书：

```bash
bash tools/check-certs.sh
```

换证书：替换 `chain.pem` / `leaf.key.pem` 后重新编译。

`tools/gen-certs.sh` 生成的是自签证书，**只能在电脑上用 `curl --cacert` 调试固件**，
车机不会信任。

---

## 已知限制

- **未命中的 SNI 直接关闭连接**，不代为转发。无上行时后台应用（百度地图、语音）会持续重试，
  实测峰值约 20 连接/秒，因此 443 设了 **6 个并发槽位**，超限的连接立即关闭，
  避免 12KB 任务栈把内部 RAM 打光。
- 商店之外的联机功能（在线音乐、地图路况等）在纯 AP 环境下不可用，属预期行为。
- APK 与 JSON 都编进固件，换 APK 需要重新编译烧录。

---

## 工作原理

1. 车机连上 AP，DHCP 下发 `192.168.2.1` 作为网关和 DNS
2. 车机探测 `http://204.z-onesoft.com/`，DNS 指向本机，80 端口回 204 → 判定联网正常
3. 商店请求 `prod-r-platform.z-onesoft.com`，DNS 指向本机，443 端口按 SNI 命中规则，
   用内置证书完成 TLS 握手，直接返回本地 JSON
4. 详情页里的 `appResourceUri` 指向 CDN 域名，同样被劫持，APK 从 flash 读出下发
5. Android 的 APK 校验只看包名与签名，与下载来源无关，装完即生效
