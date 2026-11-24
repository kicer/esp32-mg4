#!/usr/bin/env python3
"""把 bootloader / partition-table / app 合成一个可直接烧到 0x0 的镜像。

用法: python3 tools/merge_bins.py [dist目录]

合并规则就是 ESP32 的默认布局（与 partitions.csv / sdkconfig 一致）：
    0x000000  bootloader.bin
    0x008000  partition-table.bin
    0x010000  esp32-mg4.bin
空隙填 0xFF，与 flash 擦除后的状态一致。
"""
import os
import sys

LAYOUT = [
    (0x000000, "bootloader.bin"),
    (0x008000, "partition-table.bin"),
    (0x010000, "esp32-mg4.bin"),
]
OUT_NAME = "esp32-mg4-full.bin"


def main():
    dist = sys.argv[1] if len(sys.argv) > 1 else "dist"
    parts = []
    for off, name in LAYOUT:
        path = os.path.join(dist, name)
        if not os.path.exists(path):
            print(f"缺少 {path}")
            return 1
        parts.append((off, path))

    end = max(off + os.path.getsize(p) for off, p in parts)
    buf = bytearray(b"\xff" * end)
    for off, path in parts:
        with open(path, "rb") as f:
            data = f.read()
        buf[off:off + len(data)] = data
        print(f"  {os.path.basename(path):22} 0x{off:06X}  {len(data):>9} bytes")

    out = os.path.join(dist, OUT_NAME)
    with open(out, "wb") as f:
        f.write(buf)
    print(f"\n合并完成: {out}  {end} bytes")
    print(f"烧录: esptool -c esp32s3 -p <PORT> -b 460800 write-flash "
          f"--flash-mode dio --flash-freq 80m --flash-size 8MB 0x0 {OUT_NAME}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
