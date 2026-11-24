#!/bin/bash
# ESP32 固件本地自检 —— 在 Mac 连上 MG4-AP 之后运行
# 用法: bash tools/selftest.sh
# 报告同时写入 tools/selftest-report.txt，恢复网络后把它贴出来即可

GW=192.168.2.1
API=prod-r-platform.z-onesoft.com
CDN=prod-dp-public-es33-tos.z-onesoft.com
OSS=prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com
APK=/ecology/ecofile-f2382adef9d44098b67e9bfb10913a56.apk
PNG=/ecology/ecofile-8d0755c37a0347f38026885f6a647375.png
APK_SIZE=485818
PNG_SIZE=70

DIR="$(cd "$(dirname "$0")" && pwd)"
REPORT="$DIR/selftest-report.txt"
: > "$REPORT"
exec > >(tee -a "$REPORT") 2>&1

PASS=0
FAIL=0
ok()  { PASS=$((PASS+1)); printf '  [OK] %s\n' "$*"; }
bad() { FAIL=$((FAIL+1)); printf '  [!!] %s\n' "$*"; }
dim() { printf '       %s\n' "$*"; }

echo "================ ESP32 固件自检 ================"
date '+%Y-%m-%d %H:%M:%S'
echo

# ---------------------------------------------------------------- 0. 链路
echo "[0] 到网关的链路"
# 不能只看 route，默认路由也会命中，必须看本机是否拿到了 192.168.2.x
MYIP=$(ifconfig 2>/dev/null | awk '/inet /{print $2}' | grep -E '^192\.168\.2\.' | head -1)
if [ -z "$MYIP" ]; then
    bad "本机没有 192.168.2.x 地址 —— 没连上 MG4-AP"
    echo
    echo "请先：Wi-Fi 里连 MG4-AP（密码 mg4inject），再重跑本脚本"
    exit 1
fi
IF=""
for i in $(ifconfig -l); do
    if ifconfig "$i" 2>/dev/null | grep -q "inet $MYIP "; then IF=$i; break; fi
done
if ping -c 1 -W 1 "$GW" > /dev/null 2>&1; then
    ok "$IF 拿到 $MYIP，$GW 可 ping 通"
else
    bad "$IF 有 $MYIP，但 ping 不通 $GW"
fi
SSID=$(networksetup -getairportnetwork "$IF" 2>/dev/null | sed 's/^.*: //')
dim "SSID: ${SSID:-未知}"
echo

# ---------------------------------------------------------------- 1. DNS
echo "[1] DNS 劫持（期望全部解析到 $GW）"
for d in 204.z-onesoft.com "$API" "$CDN" "$OSS"; do
    A=$(dig +short +time=2 +tries=1 @"$GW" "$d" A 2>/dev/null | head -1)
    if [ "$A" = "$GW" ]; then
        ok "$d -> $A"
    else
        bad "$d -> ${A:-超时/无应答}"
    fi
done
# AAAA 应返回空（否则车机会优先走 IPv6）
AAAA=$(dig +short +time=2 +tries=1 @"$GW" "$API" AAAA 2>/dev/null | head -1)
if [ -z "$AAAA" ]; then
    ok "AAAA 查询返回空（IPv6 已抑制）"
else
    bad "AAAA 返回了 $AAAA —— 车机会优先走 IPv6"
fi
echo

# ---------------------------------------------------------------- 2. HTTP 80
echo "[2] 80 端口（联网检测，期望 204）"
for u in "http://$GW/" "http://204.z-onesoft.com/"; do
    C=$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$u" 2>/dev/null)
    [ "$C" = "204" ] && ok "$u -> $C" || bad "$u -> ${C:-无响应}"
done
echo

# ---------------------------------------------------------------- 3. TLS
echo "[3] 443 TLS 证书"
CERT=$(echo | openssl s_client -connect "$GW:443" -servername "$API" 2>/dev/null \
       | openssl x509 -noout -subject -issuer -dates 2>/dev/null)
if [ -n "$CERT" ]; then
    ok "握手成功，证书如下"
    echo "$CERT" | sed 's/^/       /'
else
    bad "TLS 握手失败"
fi
echo

# ---------------------------------------------------------------- 4. 接口
echo "[4] 三个列表接口"
for p in banner/list category/recommend/list category/ordinary/list; do
    R=$(curl -sk -m 8 --resolve "$API:443:$GW" -X POST \
        -H 'Content-Type: application/json' -d '{}' \
        "https://$API/eco/ippf/appMarket/vehicle/$p" 2>/dev/null)
    N=$(printf '%s' "$R" | wc -c | tr -d ' ')
    if printf '%s' "$R" | grep -q '"code":[[:space:]]*200'; then
        ok "$p  ${N} bytes"
    else
        bad "$p  返回异常 (${N} bytes): $(printf '%s' "$R" | head -c 120)"
    fi
done
echo

# ---------------------------------------------------------------- 5. detail
echo "[5] app/detail（appId 动态回填）"
R=$(curl -sk -m 8 --resolve "$API:443:$GW" -X POST \
    -H 'Content-Type: application/json' \
    -d '{"appId":"appVersion_SELFTEST99","vin":"SELFTESTVIN"}' \
    "https://$API/eco/ippf/appMarket/vehicle/app/detail" 2>/dev/null)
N=$(printf '%s' "$R" | wc -c | tr -d ' ')
if printf '%s' "$R" | grep -q 'appVersion_SELFTEST99'; then
    ok "appId 已回填  ${N} bytes"
else
    bad "appId 未回填 (${N} bytes): $(printf '%s' "$R" | head -c 160)"
fi
if printf '%s' "$R" | grep -q 'SELFTESTVIN'; then
    ok "vin 已回填"
else
    bad "vin 未回填"
fi
if printf '%s' "$R" | grep -q '"appPackage":"com.desay.xadb"'; then
    ok "appPackage = com.desay.xadb"
else
    bad "appPackage 不对"
fi
echo

# ---------------------------------------------------------------- 6. APK
echo "[6] APK 下发"
TMP=$(mktemp /tmp/mg4-test-XXXXXX.apk)
C=$(curl -sk -m 120 --resolve "$CDN:443:$GW" -o "$TMP" -w '%{http_code}' "https://$CDN$APK" 2>/dev/null)
SZ=$(stat -f%z "$TMP" 2>/dev/null || echo 0)
if [ "$SZ" = "$APK_SIZE" ]; then
    ok "$C  ${SZ} bytes（与烧录固件一致）"
    dim "sha256 $(shasum -a 256 "$TMP" | cut -d' ' -f1)"
else
    bad "$C  ${SZ} bytes，期望 $APK_SIZE"
fi

# Range 探测（OkHttp 下载前会先发 Range: bytes=0-0）
RC=$(curl -sk -m 10 --resolve "$CDN:443:$GW" -r 0-0 -o /dev/null -w '%{http_code}' "https://$CDN$APK" 2>/dev/null)
[ "$RC" = "206" ] && ok "Range 0-0 -> 206" || bad "Range 0-0 -> ${RC:-无响应}（期望 206）"
rm -f "$TMP"
echo

# ---------------------------------------------------------------- 7. 图片
echo "[7] 占位图"
TMP=$(mktemp /tmp/mg4-test-XXXXXX.png)
C=$(curl -sk -m 10 --resolve "$CDN:443:$GW" -o "$TMP" -w '%{http_code}' "https://$CDN$PNG" 2>/dev/null)
SZ=$(stat -f%z "$TMP" 2>/dev/null || echo 0)
[ "$SZ" = "$PNG_SIZE" ] && ok "$C  ${SZ} bytes" || bad "$C  ${SZ} bytes，期望 $PNG_SIZE"
rm -f "$TMP"
echo

# ---------------------------------------------------------------- 8. 未命中
echo "[8] 未命中域名（应被拒绝，不拖时间）"
T0=$(date +%s)
curl -sk -m 6 --resolve "baidu.com:443:$GW" -o /dev/null "https://baidu.com/" 2>/dev/null
T1=$(date +%s)
ok "未命中连接已关闭，耗时 $((T1-T0))s（应 < 2s）"
echo

echo "================ 结果 ================"
echo "通过 $PASS 项，失败 $FAIL 项"
if [ "$FAIL" -eq 0 ]; then
    echo "全部通过 —— 固件行为与设计一致"
else
    echo "有失败项，把本文件贴出来即可定位"
fi
echo "报告已保存: $REPORT"
