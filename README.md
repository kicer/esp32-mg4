# esp32-mg4

MG4 车机的应用商店在下载应用时，请求改由一块 ESP32-S3 本地应答，使指定的 APK 能够安装进车机。
整个过程不需要互联网连接。

---

## 一、硬件

在购物网站搜索 **`esp32s3 N16R8`**，单价约 20 元。

下单前确认商品参数满足以下条件：

| 项 | 要求 |
|---|---|
| 芯片 | ESP32-S3 |
| Flash | ≥ 8MB |
| PSRAM | 8MB |

*N16R8 即 16MB Flash + 8MB PSRAM，是这套固件的推荐规格。*

## 二、下载固件

**esp32-mg4-full.bin**（约 1.4MB，单文件镜像）

```
https://git.foresh.com/kicer/esp32-mg4/releases/download/v0.4/esp32-mg4-full.bin
```

完整发布页：https://git.foresh.com/kicer/esp32-mg4/releases

## 三、烧录（Windows）

使用乐鑫官方的 Flash Download Tool。

下载地址：

```
https://docs.espressif.com/projects/esp-test-tools/zh_CN/latest/esp32/production_stage/tools/flash_download_tool.html
```

操作步骤：

1. 解压下载的压缩包，运行 `flash_download_tool_x.x.x.exe`
2. **ChipType** 选择 `ESP32-S3`，**WorkMode** 选择 `Develop`，点击 OK
3. 按以下参数填写：

| 项 | 值 |
|---|---|
| 文件框 | 选择 `esp32-mg4-full.bin` |
| 地址框 | `0x0` |
| SPI SPEED | `80MHz` |
| SPI MODE | `DIO` |
| Flash SIZE | `8MB` |
| COM | 板子对应的串口 |

4. 勾选文件行左侧的复选框
5. 点击 **START**，等待进度完成
6. 按一下板子上的 **RESET** 键

## 四、烧录（macOS / Linux）

```bash
pip install esptool

esptool -c esp32s3 -p /dev/cu.usbmodem* -b 460800 \
  write-flash --flash-mode dio --flash-freq 80m --flash-size 8MB \
  0x0 esp32-mg4-full.bin
```

*Linux 上串口通常为 `/dev/ttyACM0`。*

## 五、车机操作

1. ESP32 上电，等待约 5 秒，Wi-Fi 列表中出现 **`MG4-AP`**
2. 车机连接 `MG4-AP`，密码 **`mg4inject`**
3. 打开应用商店，选择 MG4 无线 ADB，点击安装
4. 安装完成后，断开 `MG4-AP` ，连接手机热点
5. 打开 MG4 无线 ADB，点击「开启无线ADB」，等待状态显示已连接
6. 扫码安装你自己的 APK 即可

---

---

# 技术细节

以下内容供开发参考。

## 提供哪些服务

| 端口 | 协议 | 作用 |
|---|---|---|
| 53 | UDP | 劫持 4 个商店域名 → `192.168.2.1`，其余回 NXDOMAIN，AAAA 一律空应答 |
| 80 | TCP | 任何请求回 `204`（车机联网探测） |
| 443 | TCP | 读 ClientHello 取 SNI：命中商店域名走本地应答，其余立即关闭 |

四个业务接口全部本地应答：

```
POST /eco/ippf/appMarket/vehicle/banner/list
POST /eco/ippf/appMarket/vehicle/category/recommend/list
POST /eco/ippf/appMarket/vehicle/category/ordinary/list
POST /eco/ippf/appMarket/vehicle/app/detail      ← appId 从请求体动态回填
GET  /ecology/*.apk                              ← 本地 APK，支持 Range/206
GET  /ecology/*.png                              ← 1×1 占位图
```

## 工作原理

1. ESP32 开 SoftAP，网关 `192.168.2.1`，DHCP 只下发 IPv4 DNS（不发 RA，车机拿不到 IPv6 路由，避免绕过）
2. 车机连上后，所有商店域名被 DNS 劫持到 ESP32
3. 443 端口读到 SNI 后分流：`prod-r-platform` / `prod-dp-public-es33*` 走本地，其余直接关闭连接
4. 命中规则的请求解析 HTTP 后按路径返回本地内容
5. 详情页里的 `appResourceUri` 指向 CDN 域名，同样被劫持，APK 从 flash 读出下发
6. Android 的 APK 校验只看包名与签名，与下载来源无关，安装后即生效

## 目录结构

```
esp32-mg4/
├── main/
│   ├── main.c          SoftAP 起在 192.168.2.1，启动各服务
│   ├── dns_server.c    UDP 53 劫持
│   ├── http80.c        TCP 80 一律回 204
│   ├── tls443.c        TCP 443：SNI 分流 + HTTP 应答 + TLS
│   ├── rules.c         域名/路径匹配、JSON 模板、资源 blob
│   └── assets/         4 个 JSON + APK + 占位图 + 证书链/私钥
├── tools/              构建与运维脚本
├── sdkconfig.defaults  目标配置
├── partitions.csv      分区表（app 6MB）
└── .github/workflows/  CI：打 tag 时编译并发布
```

## 工具

| 脚本 | 用途 |
|---|---|
| `tools/deploy.sh` | 拉最新 release、校验、烧录、起串口监控 |
| `tools/flash.sh` | 烧录本地 `dist/` 中的镜像 |
| `tools/serial-log.sh [秒]` | 读取串口输出若干秒 |
| `tools/serial-watch.sh` | 后台持续记录串口到 `logs/esp32-serial.log` |
| `tools/selftest.sh` | 19 项端到端自检（需连上 MG4-AP 后运行） |
| `tools/check-certs.sh` | 检查证书链、SAN 覆盖范围、私钥配对 |
| `tools/gen-certs.sh` | 生成自签证书（仅限本机调试，车机不信任） |
| `tools/make-assets.py` | 校验或生成 `main/assets/` 下的 JSON |
| `tools/merge_bins.py` | 将三个 bin 合并为单文件镜像 |
| `tools/release-gitea.sh <tag>` | 发布到自建 Gitea 的 Releases |

## 自检

连接 `MG4-AP` 后运行：

```bash
bash tools/selftest.sh
```

检查 19 项：DNS 劫持、80 端口 204、TLS 证书、三个列表接口、detail 回填、APK 下发与 Range、占位图、未命中域名的处理。结果同时写入 `tools/selftest-report.txt`。

## 自己编译

CI 仅在打 tag（`v*`）时编译，普通 push 不触发：

```bash
git tag -a v0.5 -m "..." && git push origin v0.5
```

编译成功后自动创建 release，产物包含单文件镜像。

本地编译需安装 ESP-IDF v5.2：

```bash
idf.py set-target esp32s3
idf.py build
python3 tools/merge_bins.py build
```

## 证书

`main/assets/chain.pem`（4 张）与 `leaf.key.pem`（RSA 2048）由原厂中间证书签发，车机信任库认可。使用 `tools/check-certs.sh` 可以核对：

- 叶子证书 SAN 使用 `*.z-onesoft.com` 通配，覆盖全部 4 个商店域名
- 私钥与叶子证书配对
- 有效期至 2027-11

更换证书只需替换这两个文件后重新编译。自签证书车机不信任，`tools/gen-certs.sh` 生成的证书仅适用于本机调试。

## 已知限制

- Flash 需 8MB 以上、PSRAM 需 8MB Octal，配置固定在 `sdkconfig.defaults`
- 仅有一套证书，只对未更新信任库的车机有效
- 车机每 11 秒重发一次 `POST /cvr/account/checkVehicleActivation`，本地无对应规则（返回 404），不影响安装
- TLS 并发上限 6 个连接，超出直接关闭

## 参考

**上游项目**

整套规则模型来自 [MG4-Playground/MG4-APK-Injector](https://github.com/MG4-Playground/MG4-APK-Injector) —— MG4 EV 上基于 eMudhra 证书链的免 ADB 第三方 APK 安装实现（Windows 版，Python 代理 + 移动热点）。

沿用的设计：

- `store_rules.json` 的 serve / patch 两段结构，以及四个接口的响应模板
- 单张多 SAN 证书 + 单一 SSLContext，不按 SNI 切换 context
- 命中规则不回源；detail 的 `appId` 从请求体动态回填
- 商店下载先发 `Range: bytes=0-0` 探长度，再全量取

区别在形态：上游依赖 Windows 移动热点 + hosts 劫持 + U 形转发，本固件让 ESP32 自身作为 AP，无需劫持与转发。

**技术依赖**

| 项目 | 用途 |
|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) v5.2 | 固件框架、SoftAP、lwIP、FreeRTOS |
| [mbedTLS](https://github.com/Mbed-TLS/mbedtls) | TLS 服务端、证书链、RSA 硬件加速 |
| [esptool](https://github.com/espressif/esptool) v5 | 烧录与镜像合并 |

**协议细节**

- TLS ClientHello 的 SNI 扩展（`server_name`, type `0x0000`），用于分流前判断 Host
- HTTP/1.1 Range：OkHttp 先发 `Range: bytes=0-0` 探长度，期望 `206` + `Content-Range`
- Android captive portal 检测：`http://204.z-onesoft.com/`，期望 `204`
- DHCP 只下发 IPv4 DNS，不发 RA，避免车机走 IPv6 绕过本地服务
