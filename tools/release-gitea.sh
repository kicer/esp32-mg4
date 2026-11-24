#!/bin/bash
# 把 dist/ 里的固件发布到 Gitea Releases
#
# 用法:
#   GITEA_PASS=xxx bash tools/release-gitea.sh v0.2 [dist]
#   不带 GITEA_PASS 会提示输入
set -e
OWNER="${GITEA_OWNER:-kicer}"
USER="${GITEA_USER:-kicer}"
REPO="${GITEA_REPO:-esp32-mg4}"
BASE="${GITEA_BASE:-https://git.foresh.com}"
VER="$1"
DIR="${2:-dist}"

[ -z "$VER" ] && { echo "用法: $0 <tag, 如 v0.2> [dist目录]"; exit 1; }
[ -d "$DIR" ] || { echo "找不到目录 $DIR"; exit 1; }

if [ -z "${GITEA_PASS:-}" ]; then
    printf "Gitea 密码: "
    read -rs GITEA_PASS
    echo
fi
AUTH="$USER:$GITEA_PASS"
API="$BASE/api/v1/repos/$OWNER/$REPO"

# 已有同 tag 的 release 就复用，没有就建
RID=$(curl -s -m 30 -u "$AUTH" "$API/releases" | python3 -c "
import sys, json
tag = '$VER'
for r in json.load(sys.stdin):
    if r.get('tag_name') == tag:
        print(r['id']); break
" 2>/dev/null)

if [ -z "$RID" ]; then
    RID=$(curl -s -m 30 -u "$AUTH" -X POST -H "Content-Type: application/json" \
        -d "{\"tag_name\":\"$VER\",\"target_commitish\":\"main\",\"name\":\"$VER\",\"draft\":false,\"prerelease\":false}" \
        "$API/releases" | python3 -c "import sys,json; print(json.load(sys.stdin).get('id',''))")
    echo "创建 release $VER (id=$RID)"
else
    echo "复用已有 release $VER (id=$RID)"
fi
[ -z "$RID" ] && { echo "创建 release 失败"; exit 1; }

# Gitea 不检查同名附件，重复上传会得到两份，先列出来跳过
EXISTING=$(curl -s -m 30 -u "$AUTH" "$API/releases/$RID" | python3 -c "
import sys, json
for a in json.load(sys.stdin).get('assets', []):
    print(a['name'])
" 2>/dev/null)

for f in "$DIR"/*.bin "$DIR"/SHA256SUMS; do
    [ -f "$f" ] || continue
    name=$(basename "$f")
    if printf '%s\n' "$EXISTING" | grep -qx "$name"; then
        echo "  $name 已存在, 跳过"
        continue
    fi
    code=$(curl -s -o /tmp/rel.out -w "%{http_code}" -m 300 -u "$AUTH" -X POST \
        -F "attachment=@$f" "$API/releases/$RID/assets?name=$name")
    case "$code" in
        201) echo "  $name 已上传" ;;
        409) echo "  $name 已存在, 跳过" ;;
        *)   echo "  $name HTTP $code  $(head -c 100 /tmp/rel.out | tr -d '\n')" ;;
    esac
done

echo "完成: $BASE/$OWNER/$REPO/releases/tag/$VER"
