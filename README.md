# esp32-mg4

ESP32-S3 固件：开一个**没有上行**的热点，把车机应用商店的列表、详情、APK 下载
全部在本地应答。车机连上它就能完成安装，全程不需要连接互联网。

面向上汽 MG4（车机代号 `ah4`，高通 SA8155P，Android 11 / SDK 30）。
原理是应用商店的几个接口都可被本地数据替换，安装时的包名校验也由列表数据决定，
所以只要把「接口响应」和「APK 下发」都搬到本地就能闭环。

## 硬件

| | |
|---|---|
| 芯片 | ESP32-S3 |
| PSRAM | 8MB（Octal） |
| Flash | 8MB（分区表按 8MB 配置，更大也能用） |
| 接口 | USB-Serial/JTAG，烧录端口形如 `/dev/cu.usbmodem*` |

## 结构

```
main/
├── main.c          NVS → AP(192.168.2.1) → 启动各服务
├── dns_server.c    UDP 53：4 个域名解析到本机，其余 NXDOMAIN
├── http80.c        TCP 80：任何请求回 204
├── tls443.c        TCP 443：按 SNI 分流，命中的走本地 TLS 响应
├── rules.c         域名/路径匹配、JSON 模板替换、资源 blob
└── assets/
    ├── detail.json / banner.json / recommend.json / ordinary.json
    ├── mg4-adb.apk        要装进车机的 APK
    ├── placeholder.png    1×1 占位图（图标/截图请求都回它）
    ├── chain.pem          证书链
    └── leaf.key.pem       证书私钥
tools/
├── flash.sh        烧录
├── check-certs.sh  检查证书链、SAN 覆盖、私钥配对
├── gen-certs.sh    生成自签证书（仅本机调试用，上机无效）
└── make-assets.py  校验/生成响应 JSON
```

## 编译

推送到 GitHub 后 Actions 自动构建（ESP-IDF v5.2 / esp32s3），产物：

```
bootloader/bootloader.bin
partition_table/partition-table.bin
esp32-mg4.bin
```

本地编译（需 ESP-IDF 5.2）：

```bash
idf.py set-target esp32s3
idf.py build
```

## 烧录

```bash
./tools/flash.sh                                  # 默认 /dev/cu.usbmodem14301 + build/
./tools/flash.sh /dev/cu.usbmodem14301 ../build   # 指定端口和产物目录
```

偏移：`0x0` bootloader、`0x8000` 分区表、`0x10000` 应用。

## 使用

1. 上电，串口 115200 应看到：

   ```
   AP 已启动: SSID=MG4-AP  IP=192.168.2.1
   DNS 服务启动 :53
   HTTP 204 服务启动 :80
   证书就绪，启动监听
   TLS 服务启动 :443
   ```

2. 车机 WLAN 连 `MG4-AP`，密码 `mg4inject`。若提示「无法访问互联网」，选**保持连接**。
3. 等约 10 秒（车机要跑完一轮联网探测），进应用商店 → 目标应用详情页 → 安装。

## 配置

`main/main.c` 顶部：

```c
#define AP_SSID "MG4-AP"
#define AP_PASS "mg4inject"   // WPA2 要求至少 8 位
#define AP_CHAN 6
```

AP 的地址固定 `192.168.2.1`。响应 JSON 里的资源 URL 用的是域名而不是 IP，
所以换 IP 也不用改数据，只要 DNS 把它们指过来就行。

## 证书

`main/assets/chain.pem` 是 4 张证书（叶子 + 两张中间 + 交叉签名根），
`leaf.key.pem` 是对应私钥。这套证书由一条已并入各大根库的中间证书签发，
车机系统的信任库里有它，所以**车机上不需要装任何 CA**。

限制：该中间证书已于 2026-10 被吊销并从根库移除，因此这套证书只对
**未更新过信任库的旧车机**有效。车机一旦更新了系统证书，需要换成它信任的证书。

检查当前证书：

```bash
./tools/check-certs.sh
```

会列出链上每张证书的主题/签发者/有效期、SAN 是否覆盖 4 个域名、私钥是否配对。

换证书只需替换 `main/assets/chain.pem` 和 `leaf.key.pem` 后重新编译。
`tools/gen-certs.sh` 生成的是自签 CA + 叶子，只能用于本机 `curl --cacert` 调试，
**上机会握手失败**（车机不认这个 CA）。

## 工作原理

```
车机连 AP
  ├─ DHCP 拿到 192.168.2.x，DNS = 192.168.2.1
  ├─ 联网探测 http://204.z-onesoft.com/  → 80 端口一律回 204 → 车机判「有网」
  └─ 应用商店
       ├─ DNS 把 4 个商店域名解析到 192.168.2.1
       ├─ 443 分流：读 ClientHello 取 SNI
       │    ├─ 商店域名 → mbedTLS 握手 → 按路径回本地数据
       │    └─ 其他域名 → 直接关连接（无上行，不转发）
       └─ 路径路由
            /eco/ippf/appMarket/vehicle/banner/list                → banner.json
            /eco/ippf/appMarket/vehicle/category/recommend/list    → recommend.json
            /eco/ippf/appMarket/vehicle/category/ordinary/list     → ordinary.json
            /eco/ippf/appMarket/vehicle/app/detail                 → detail.json（appId 从请求体填）
            /ecology/*.apk                                         → mg4-adb.apk（支持 Range）
            /ecology/*.png                                         → placeholder.png
```

几个必须注意的点：

- **不能启用 IPv6**。车机拿到 IPv6 默认路由后，HTTP 会绕开 IPv4 的 DNS 劫持和
  TLS 分流，出现「列表能加载、图片和详情页卡住」的现象。本工程 `CONFIG_LWIP_IPV6`
  保持关闭，SoftAP 也不发 RA。
- **80 端口必须在听**。它只服务 204 探测，但少了它车机会判定这个 Wi-Fi 没网。
- **detail 的 `appId` 必须来自请求体**。写死会导致详情页空白，并在车机上抛
  `lateinit property mDetailApp has not been initialized`。
- **列表接口要一起改**。安装时 `PackageInstaller` 会校验「详情页的 appPackage
  是否等于 APK 实际包名」，只改 detail 不改列表会得到 `INSTALL_FAILED_INVALID_APK`。
- **字段类型要和原始响应一致**。例如 `appVersionCode` 是字符串 `"25"`，改成数字会让
  Kotlin 反序列化失败。

## 工具

```bash
./tools/make-assets.py                              # 校验 assets 里的 JSON
./tools/make-assets.py ../store_rules.json          # 从规则文件重新生成
./tools/check-certs.sh                              # 检查证书链
./tools/gen-certs.sh                                # 生成自签证书（调试用）
```

`make-assets.py` 会顺带检查 `appVersionCode` 是不是字符串、detail 模板的
`__APPID__` / `__VIN__` 占位符是否还在。

## 已知限制

- 每个 TLS 连接占 12KB 任务栈，并发数受内存限制，车机通常只开 2-4 个连接
- 后台应用（语音、地图、音乐）会持续重连并被关闭，属正常现象，不影响商店
- 只对未更新信任库的车机有效（见「证书」）
- 换 APK 需要改 `main/assets/mg4-adb.apk` 并同步更新响应 JSON 里的
  `appResourceUri`、`appPackage`、`appVersionCode`

## 免责

仅用于自有车辆的调试与学习。注入的是自己的 APK，改的是自己车机的商店响应。
