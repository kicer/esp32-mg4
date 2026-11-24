#!/usr/bin/env python3
"""校验 / 生成 main/assets 下的响应 JSON。

两种用法：
  ./tools/make-assets.py                       # 只校验现有 assets
  ./tools/make-assets.py /path/store_rules.json  # 从规则文件重新生成

规则文件格式（Mac 侧 proxy_server.py 用的那份）：
  {"patch": [{"path_prefix": "...", "respond": {...}}, ...]}
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "main" / "assets"

# 路径前缀 → 资源文件名
MAP = {
    "/eco/ippf/appMarket/vehicle/app/detail": "detail.json",
    "/eco/ippf/appMarket/vehicle/banner/list": "banner.json",
    "/eco/ippf/appMarket/vehicle/category/recommend/list": "recommend.json",
    "/eco/ippf/appMarket/vehicle/category/ordinary/list": "ordinary.json",
}

PLACEHOLDERS = ('__APPID__', '__VIN__')


def from_rules(path: Path) -> None:
    rules = json.loads(path.read_text(encoding="utf-8"))
    done = set()
    for p in rules.get("patch", []):
        pre = p.get("path_prefix", "")
        fn = MAP.get(pre)
        if not fn or not isinstance(p.get("respond"), dict):
            continue
        obj = p["respond"]
        s = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
        if fn == "detail.json":
            # 详情页的 appId 必须来自请求体，写死会崩（mDetailApp 未初始化）
            if not s.endswith("}}"):
                raise SystemExit(f"{fn}: 顶层结构异常，结尾不是 }}}}")
            s = s[:-2] + ',"appId":"__APPID__","vin":"__VIN__"}}'
        (ASSETS / fn).write_text(s, encoding="utf-8")
        done.add(fn)
        print(f"  写入 {fn}: {len(s.encode('utf-8'))} bytes")
    missing = set(MAP.values()) - done
    if missing:
        print(f"  [!] 规则里缺这些接口: {', '.join(sorted(missing))}")


def verify() -> int:
    bad = 0
    for fn in MAP.values():
        f = ASSETS / fn
        if not f.exists():
            print(f"  [✗] 缺文件 {fn}")
            bad += 1
            continue
        raw = f.read_text(encoding="utf-8")
        try:
            obj = json.loads(raw.replace("__APPID__", "x").replace("__VIN__", "y"))
        except json.JSONDecodeError as e:
            print(f"  [✗] {fn} 不是合法 JSON: {e}")
            bad += 1
            continue
        size = len(raw.encode("utf-8"))
        if fn == "detail.json":
            has = all(p in raw for p in PLACEHOLDERS)
            print(f"  [{'✓' if has else '✗'}] {fn:16} {size:5} bytes  占位符{'齐全' if has else '缺失'}")
            if not has:
                bad += 1
                continue
        else:
            print(f"  [✓] {fn:16} {size:5} bytes")
        # 关键字段抽查
        if fn == "detail.json":
            d = obj.get("data", {})
            pkg = d.get("appPackage")
            print(f"      appPackage={pkg}  appVersionCode={d.get('appVersionCode')!r}")
            if d.get("appVersionCode") is not None and not isinstance(d.get("appVersionCode"), str):
                print("      [✗] appVersionCode 必须是字符串，数字会让 Kotlin 反序列化失败")
                bad += 1
    return bad


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    if len(sys.argv) > 1:
        src = Path(sys.argv[1])
        if not src.exists():
            raise SystemExit(f"找不到 {src}")
        print(f"从 {src} 生成：")
        from_rules(src)
        print()
    print("校验：")
    bad = verify()
    apk = ASSETS / "mg4-adb.apk"
    if apk.exists():
        print(f"  [✓] mg4-adb.apk      {apk.stat().st_size:5} bytes")
    else:
        print("  [✗] 缺 mg4-adb.apk")
        bad += 1
    if bad:
        raise SystemExit(f"\n{bad} 处问题")
    print("\n全部正常")


if __name__ == "__main__":
    main()
