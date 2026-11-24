#!/bin/bash
# 抓 ESP32-S3 串口输出（USB-Serial/JTAG，无需 idf.py monitor）
# 用法: bash tools/serial-log.sh [秒数] [端口]
#   默认读 15 秒，端口自动探测 /dev/cu.usbmodem*

SECS="${1:-15}"
PORT="${2:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"

if [ -z "$PORT" ]; then
    echo "找不到 /dev/cu.usbmodem* —— 板子没插？"
    exit 1
fi

echo "读取 $PORT ${SECS}s ..."
stty -f "$PORT" 115200 raw -echo 2>/dev/null

python3 - "$PORT" "$SECS" <<'PY'
import os, select, sys, time

port, secs = sys.argv[1], float(sys.argv[2])
fd = os.open(port, os.O_RDONLY | os.O_NONBLOCK)
end = time.time() + secs
buf = b""
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.3)
    if r:
        try:
            d = os.read(fd, 8192)
            if d:
                buf += d
        except BlockingIOError:
            pass
os.close(fd)
sys.stdout.write(buf.decode("utf-8", "replace"))
if not buf:
    print("(无输出：固件没在跑，或 console 仍指向 UART0)")
PY
