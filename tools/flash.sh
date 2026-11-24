#!/bin/bash
# 烧录 ESP32-S3（esptool v5 语法）
# 用法: ./tools/flash.sh [端口] [build目录]
#   ./tools/flash.sh                                  # 默认端口 + build/
#   ./tools/flash.sh /dev/cu.usbmodem14301 ../build   # 手动指定
set -euo pipefail

PORT="${1:-/dev/cu.usbmodem14301}"
DIR="${2:-build}"

BOOT="$DIR/bootloader/bootloader.bin"
PT="$DIR/partition_table/partition-table.bin"

APP=""
for f in "$DIR"/esp32-mg4.bin "$DIR"/*.bin; do
    case "$(basename "$f" 2>/dev/null || true)" in
        bootloader.bin|partition-table.bin|merged*.bin|*ota*) continue ;;
        *.bin) APP="$f"; break ;;
    esac
done

for f in "$BOOT" "$PT" "$APP"; do
    [ -f "$f" ] || { echo "[✗] 缺少 $f"; echo "    先在仓库跑 CI 下载 artifact，或本地 idf.py build"; exit 1; }
done

echo "端口:   $PORT"
echo "boot:   $BOOT"
echo "table:  $PT"
echo "app:    $APP ($(stat -f%z "$APP" 2>/dev/null || stat -c%s "$APP") bytes)"

esptool -c esp32s3 -p "$PORT" -b 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-freq 80m --flash-size 8MB \
    0x0 "$BOOT" 0x8000 "$PT" 0x10000 "$APP"

echo
echo "烧录完成。看串口输出确认服务启动："
echo "  python3 -m serial.tools.miniterm $PORT 115200"
