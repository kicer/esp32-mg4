#!/bin/bash
# 烧录 ESP32-S3
# 用法: bash tools/flash.sh [端口] [产物目录]
#   默认端口 /dev/cu.usbmodem14301，默认目录 dist/
#   有 esp32-mg4-full.bin 就一次烧完，否则按三段偏移分别烧
set -e
PORT="${1:-/dev/cu.usbmodem14301}"
DIR="${2:-dist}"

FLASH_ARGS="--flash-mode dio --flash-freq 80m --flash-size 8MB"

FULL="$DIR/esp32-mg4-full.bin"
if [ -f "$FULL" ]; then
    echo "单文件烧录: $FULL ($(stat -f%z "$FULL" 2>/dev/null || stat -c%s "$FULL") bytes)"
    esptool -c esp32s3 -p "$PORT" -b 460800 \
        --before default-reset --after hard-reset \
        write-flash $FLASH_ARGS 0x0 "$FULL"
    exit $?
fi

BOOT="$DIR/bootloader.bin"
PT="$DIR/partition-table.bin"
APP="$DIR/esp32-mg4.bin"
for f in "$BOOT" "$PT" "$APP"; do
    [ -f "$f" ] || { echo "缺少 $f"; exit 1; }
done

echo "分段烧录: $DIR"
esptool -c esp32s3 -p "$PORT" -b 460800 \
    --before default-reset --after hard-reset \
    write-flash $FLASH_ARGS \
    0x0 "$BOOT" 0x8000 "$PT" 0x10000 "$APP"
