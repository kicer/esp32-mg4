#!/usr/bin/env python3
"""持续把串口输出追加到文件（不碰 DTR/RTS）。

用法: python3 serial_watch.py <port> <outfile>
"""
import os
import select
import sys


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    port, out = sys.argv[1], sys.argv[2]
    try:
        fd = os.open(port, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as e:
        print(f"打开 {port} 失败: {e}")
        return 1

    with open(out, "a", buffering=1, encoding="utf-8", errors="replace") as f:
        while True:
            try:
                r, _, _ = select.select([fd], [], [], 1.0)
            except InterruptedError:
                continue
            if not r:
                continue
            try:
                d = os.read(fd, 8192)
            except OSError:
                continue
            if d:
                f.write(d.decode("utf-8", "replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
