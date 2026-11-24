#!/bin/bash
# 检查 main/assets/ 下的证书链与私钥是否可用
# 用法: ./tools/check-certs.sh [证书链] [私钥]
set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHAIN="${1:-$DIR/main/assets/chain.pem}"
KEY="${2:-$DIR/main/assets/leaf.key.pem}"

for f in "$CHAIN" "$KEY"; do
    [ -f "$f" ] || { echo "[✗] 找不到 $f"; exit 1; }
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "=== 证书链 ==="
# 拆成单张证书，逐张列出
awk -v d="$TMP" '/BEGIN CERTIFICATE/{n++} n{print > (d "/c" n ".pem")}' "$CHAIN"
COUNT=$(ls "$TMP"/c*.pem 2>/dev/null | wc -l | tr -d ' ')
echo "共 $COUNT 张"

LEAF=""
for f in "$TMP"/c*.pem; do
    idx=$(basename "$f" .pem | tr -d 'c')
    subj=$(openssl x509 -in "$f" -noout -subject 2>/dev/null | sed 's/^subject=//')
    iss=$(openssl x509 -in "$f" -noout -issuer 2>/dev/null | sed 's/^issuer=//')
    dates=$(openssl x509 -in "$f" -noout -dates 2>/dev/null | tr '\n' ' ')
    echo "  [$idx] $subj"
    echo "      签发者: $iss"
    echo "      $dates"
    if [ -z "$LEAF" ]; then LEAF="$f"; fi
    # 过期检查
    if ! openssl x509 -in "$f" -noout -checkend 0 >/dev/null 2>&1; then
        echo "      [✗] 已过期"
    fi
done

echo
echo "=== 域名覆盖 (SAN) ==="
openssl x509 -in "$LEAF" -noout -ext subjectAltName 2>/dev/null \
    | tail -n +2 | tr ',' '\n' | sed 's/^ *//' | sed 's/^/  /' || echo "  (无 SAN)"

SAN_TXT=$(openssl x509 -in "$LEAF" -noout -ext subjectAltName 2>/dev/null)

# 精确匹配，或 *.parent 通配符匹配（* 只覆盖一级）
san_covers() {
    local name="$1"
    if printf '%s' "$SAN_TXT" | grep -qF "DNS:${name}"; then
        return 0
    fi
    local wild="*.${name#*.}"
    if printf '%s' "$SAN_TXT" | grep -qF "DNS:${wild}"; then
        return 0
    fi
    return 1
}

for need in prod-r-platform.z-onesoft.com \
            prod-dp-public-es33-tos.z-onesoft.com \
            prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com \
            204.z-onesoft.com; do
    if san_covers "$need"; then
        echo "  [✓] $need"
    else
        echo "  [!] $need 不在 SAN 里"
    fi
done

echo
echo "=== 私钥与叶子证书是否配对 ==="
PUB_CERT=$(openssl x509 -in "$LEAF" -noout -pubkey 2>/dev/null | openssl md5)
PUB_KEY=$(openssl pkey -in "$KEY" -pubout 2>/dev/null | openssl md5)
if [ "$PUB_CERT" = "$PUB_KEY" ]; then
    echo "  [✓] 配对"
else
    echo "  [✗] 不匹配（证书与私钥不是一对）"
    exit 1
fi

echo
echo "=== 私钥类型 ==="
openssl pkey -in "$KEY" -noout -text 2>/dev/null | head -1 | sed 's/^/  /'
