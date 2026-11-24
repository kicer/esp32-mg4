#!/bin/bash
# 生成一套「自签」测试证书，用于本机 curl / 浏览器验证固件，不需要车机。
#
# 注意：车机信任库里有的是原厂那套中间证书，自签 CA 车机不认识 —— 用它上机
# 一定握手失败。上机请保留 main/assets/ 下原有的 chain.pem + leaf.key.pem。
#
# 用法:
#   ./tools/gen-certs.sh                 # 输出到 ./certs-test/
#   ./tools/gen-certs.sh /tmp/my-certs   # 指定目录
set -euo pipefail

OUT="${1:-$(cd "$(dirname "$0")/.." && pwd)/certs-test}"
mkdir -p "$OUT"

NAMES=(
    "prod-r-platform.z-onesoft.com"
    "prod-dp-public-es33-tos.z-onesoft.com"
    "prod-dp-public-es33.oss-cn-shanghai.aliyuncs.com"
    "204.z-onesoft.com"
)

SAN=""
for n in "${NAMES[@]}"; do
    SAN="${SAN}DNS:${n},"
done
SAN="${SAN%,}"

echo "[1/3] 生成 CA"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$OUT/ca.key" -out "$OUT/ca.pem" \
    -days 3650 -subj "/C=CN/O=MG4 Test/CN=MG4 Test CA" 2>/dev/null

echo "[2/3] 生成叶子证书（CN=${NAMES[0]}）"
openssl req -newkey rsa:2048 -nodes -keyout "$OUT/leaf.key" -out "$OUT/leaf.csr" \
    -subj "/C=CN/O=MG4 Test/CN=${NAMES[0]}" 2>/dev/null

printf "subjectAltName=%s\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n" "$SAN" > "$OUT/san.cnf"

openssl x509 -req -in "$OUT/leaf.csr" -CA "$OUT/ca.pem" -CAkey "$OUT/ca.key" \
    -CAcreateserial -out "$OUT/leaf.pem" -days 3650 -extfile "$OUT/san.cnf" 2>/dev/null

echo "[3/3] 合成 chain.pem（叶子 + CA）"
cat "$OUT/leaf.pem" "$OUT/ca.pem" > "$OUT/chain.pem"

rm -f "$OUT/leaf.csr" "$OUT/san.cnf" "$OUT/ca.srl"

echo
echo "输出目录: $OUT"
ls -l "$OUT" | sed 's/^/  /'
echo
echo "本机验证固件："
echo "  curl --cacert $OUT/ca.pem --resolve prod-r-platform.z-onesoft.com:443:192.168.2.1 \\"
echo "       -k -X POST https://prod-r-platform.z-onesoft.com/eco/ippf/appMarket/vehicle/banner/list"
echo
echo "换成自签证书上机（会失败，仅调试用）："
echo "  cp $OUT/chain.pem $OUT/leaf.key main/assets/ && ./tools/flash.sh"
