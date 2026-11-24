# 完整流程日志（v0.4）

抓取时间：2026-11-24
设备：ESP32-S3（8MB flash / 8MB PSRAM），固件 v0.4
串口：USB-Serial/JTAG，115200
车机：MG4（`ah4`，SA8155P，Android 11）

这份日志对应一次完整的真实装机过程：车机连上 `MG4-AP`，进商店，打开详情页，点安装，安装成功。
全程 ESP32 没有任何上行链路，所有响应都由本地生成。

## 时间线

| 时刻 (ms) | 事件 |
|---|---|
| 36162 | 车机连上 AP，DHCP 分配 `192.168.2.2` |
| 41252 | 劫持 `204.z-onesoft.com` → `192.168.2.1`（联网探测） |
| 50032 | 劫持 CDN 域名，APK 请求到达（`Range: bytes=0-0` 探长度） |
| 51192 | `[serve]` 返回 206 + 1 字节 |
| 52362 | APK 全量请求（`Range: bytes=0-485817`） |
| 53002 | `[serve]` 485818 字节投递完成 |
| 147322 | 劫持 API 域名，`checkVehicleActivation`（本地无规则，回 404） |
| 206662 | `app/detail` 请求 |
| 206672 | `[patch]` 本地响应 1696 字节，`appId` 从请求体回填 |
| 209622 | 详情页 4 张图标全部命中本地占位图 |
| 218472 | 再次请求 APK（这次是安装流程） |
| 220352 | `[serve]` 485818 字节投递完成，随后车机安装成功 |

## 观察到的现象

- 车机每 11 秒重发一次 `POST /cvr/account/checkVehicleActivation`，本地没有对应规则（`rule=0`），回 404。
  不影响安装，但属于可以消掉的噪音。
- 两次 APK 请求都带 `Range`，本地正确返回 `206` + `Content-Range`。
- 全程没有 `-0x4380` 之类的握手错误（v0.4 修复了并发共享私钥上下文的问题）。

## 原始日志

```
I (36162) wifi:station: ec:a7:ad:72:09:c6 join, AID=1, bgn, 40U
I (36262) esp_netif_lwip: DHCP server assigned IP to a client, IP is: 192.168.2.2
I (41252) dns: [HIJACK] 204.z-onesoft.com A -> 本地 (192.168.2.1)
I (50032) dns: [HIJACK] prod-dp-public-es33-tos.z-onesoft.com A -> 本地 (192.168.2.1)
I (51192) tls443: GET prod-dp-public-es33-tos.z-onesoft.com/ecology/ecofile-f2382adef9d44098b67e9bfb10913a56.apk (sni=prod-dp-public-es33-tos.z-onesoft.com, rule=5, range=bytes=0-0)
I (51192) tls443: [serve] APK 485818 bytes (range 0-0)
I (52362) tls443: GET prod-dp-public-es33-tos.z-onesoft.com/ecology/ecofile-f2382adef9d44098b67e9bfb10913a56.apk (sni=prod-dp-public-es33-tos.z-onesoft.com, rule=5, range=bytes=0-485817)
I (53002) tls443: [serve] APK 485818 bytes (range 0-485817)
I (101372) dns: [HIJACK] 204.z-onesoft.com A -> 本地 (192.168.2.1)
I (147322) dns: [HIJACK] prod-r-platform.z-onesoft.com A -> 本地 (192.168.2.1)
I (148482) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (159652) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (161602) dns: [HIJACK] 204.z-onesoft.com A -> 本地 (192.168.2.1)
I (170832) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (182012) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (193172) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (204332) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (206662) tls443: POST prod-r-platform.z-onesoft.com/eco/ippf/appMarket/vehicle/app/detail (sni=prod-r-platform.z-onesoft.com, rule=1, range=-)
I (206672) tls443: [patch] detail 1696 bytes (appId=appVersion_5296577779180211)
I (206802) dns: [HIJACK] prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com A -> 本地 (192.168.2.1)
I (209622) tls443: GET prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com/ecology/ecofile-e66a15e186ae4103b843acb4a2b8032c.png (sni=prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com, rule=6, range=-)
I (209622) tls443: GET prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com/ecology/ecofile-acbde140bf974805bdff80cd0e95814c.png (sni=prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com, rule=6, range=-)
I (209622) tls443: GET prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com/ecology/ecofile-8d0755c37a0347f38026885f6a647375.png (sni=prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com, rule=6, range=-)
I (209652) tls443: GET prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com/ecology/ecofile-ae0b6f407bd448f9bba9e61c1f31cb7e.png (sni=prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com, rule=6, range=-)
I (214352) dns: [HIJACK] prod-r-platform.z-onesoft.com A -> 本地 (192.168.2.1)
I (215502) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (217292) dns: [HIJACK] prod-dp-public-es33-tos.z-onesoft.com A -> 本地 (192.168.2.1)
I (218472) tls443: GET prod-dp-public-es33-tos.z-onesoft.com/ecology/ecofile-f2382adef9d44098b67e9bfb10913a56.apk (sni=prod-dp-public-es33-tos.z-onesoft.com, rule=5, range=bytes=0-0)
I (218472) tls443: [serve] APK 485818 bytes (range 0-0)
I (219632) tls443: GET prod-dp-public-es33-tos.z-onesoft.com/ecology/ecofile-f2382adef9d44098b67e9bfb10913a56.apk (sni=prod-dp-public-es33-tos.z-onesoft.com, rule=5, range=bytes=0-485817)
I (220352) tls443: [serve] APK 485818 bytes (range 0-485817)
I (221842) dns: [HIJACK] 204.z-onesoft.com A -> 本地 (192.168.2.1)
I (226932) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (238102) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (249272) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (260452) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (271632) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (281662) dns: [HIJACK] prod-r-platform.z-onesoft.com A -> 本地 (192.168.2.1)
I (282032) dns: [HIJACK] 204.z-onesoft.com A -> 本地 (192.168.2.1)
I (283032) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (294212) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (305392) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (316642) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (327812) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (338982) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (342222) dns: [HIJACK] 204.z-onesoft.com A -> 本地 (192.168.2.1)
I (349002) dns: [HIJACK] prod-r-platform.z-onesoft.com A -> 本地 (192.168.2.1)
I (350152) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (361352) tls443: POST prod-r-platform.z-onesoft.com/cvr/account/checkVehicleActivation (sni=prod-r-platform.z-onesoft.com, rule=0, range=-)
I (370212) wifi:station: ec:a7:ad:72:09:c6 leave, AID = 1, bss_flags is 134243, bss:0x3fca4488
```
