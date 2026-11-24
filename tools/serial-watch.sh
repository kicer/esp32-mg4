#!/bin/bash
# 后台持续记录串口到文件，随时可 tail
# 用法: bash tools/serial-watch.sh [输出文件]
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$DIR/../logs/esp32-serial.log}"
PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)"

[ -z "$PORT" ] && { echo "没找到串口设备"; exit 1; }
mkdir -p "$(dirname "$OUT")"
: > "$OUT"

pkill -f "serial_watch.py" 2>/dev/null
sleep 0.3
nohup python3 "$DIR/serial_watch.py" "$PORT" "$OUT" > /dev/null 2>&1 &
PID=$!
echo "串口监控已启动: $PORT -> $OUT (pid $PID)"
echo "查看: tail -f $OUT"
echo "停止: kill $PID"
