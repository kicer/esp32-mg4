#!/bin/bash
# 抓 ESP32-S3 串口输出（USB-Serial/JTAG）
# 用法: bash tools/serial-log.sh [秒数] [端口]
SECS="${1:-30}"
PORT="${2:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"

if [ -z "$PORT" ]; then
    echo "找不到 /dev/cu.usbmodem* —— 板子没插？"
    exit 1
fi

echo "读取 $PORT ${SECS}s ..."
echo ">>> 现在按一下板子上的 RESET 键，日志只在重启后 1-2 秒内输出 <<<"

python3 - "$PORT" "$SECS" <<'PY'
import sys, time
try:
    import serial
except ImportError:
    print("缺 pyserial：pip3 install pyserial")
    sys.exit(1)

port, secs = sys.argv[1], float(sys.argv[2])
# 打开时不碰 DTR/RTS：有些板子把 DTR 拉到 EN，拉低会把芯片按在复位里
try:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
except Exception as e:
    print(f"打开失败: {e}")
    sys.exit(1)

buf = bytearray()
end = time.time() + secs
while time.time() < end:
    try:
        d = ser.read(4096)
    except Exception:
        d = b""
    if d:
        buf.extend(d)
ser.close()

txt = buf.decode("utf-8", "replace")
print(f"--- 收到 {len(buf)} 字节 ---")
print(txt if txt.strip() else "(无输出：固件没在跑，或 console 仍指向 UART0)")
PY
