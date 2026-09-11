#!/bin/sh
# =====================================================================
# iNES 应用图标打包脚本 (macOS)
#
# 作用: mac/icon/iNES-icon.svg  ->  mac/icon/iNES.icns
# 依赖: qlmanage / sips / iconutil —— 全部为 macOS 自带工具, 不引入第三方依赖。
#
# 用法: cd mac/icon && ./make-icns.sh
#
# 说明: 先在 1024x1024 下光栅化 SVG(qlmanage 走 WebKit 矢量渲染, 保真度最高),
#       再用 sips 逐级降采样出 Big Sur 图标网格所需的各个尺寸。
#       注意 sips 会保留 alpha, 而 ImageMagick 在小尺寸下可能丢弃 alpha, 故不用它。
# =====================================================================
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
SRC="$SCRIPT_DIR/iNES-icon.svg"
OUT="$SCRIPT_DIR/iNES.icns"
ICONSET="$SCRIPT_DIR/iNES.iconset"
BASE=1024

# ---- 前置检查 -------------------------------------------------------
[ -f "$SRC" ] || { echo "错误: 找不到源文件 $SRC" >&2; exit 1; }
for tool in qlmanage sips iconutil; do
    command -v "$tool" >/dev/null 2>&1 || { echo "错误: 缺少 $tool (macOS 自带工具)" >&2; exit 1; }
done

# SVG 必须是良构 XML: 注释中若出现连续两个连字符, WebKit 会解析失败并渲染出
# 一张白色的错误提示页, 且 qlmanage 仍返回成功 —— 故这里先做一次静态校验。
if command -v xmllint >/dev/null 2>&1; then
    xmllint --noout "$SRC" || { echo "错误: $SRC 不是良构 XML" >&2; exit 1; }
fi

TMP_DIR=$(mktemp -d)
# 无论成功失败都清理中间产物(成功时保留 .icns)
trap 'rm -rf "$TMP_DIR" "$ICONSET"' EXIT

# ---- 1. SVG -> 1024x1024 PNG ----------------------------------------
echo "渲染 $SRC ..."
qlmanage -t -s "$BASE" -o "$TMP_DIR" "$SRC" >/dev/null 2>&1 \
    || { echo "错误: qlmanage 渲染 SVG 失败" >&2; exit 1; }

BASE_PNG="$TMP_DIR/$(basename "$SRC").png"
[ -f "$BASE_PNG" ] || { echo "错误: 未生成 $BASE_PNG" >&2; exit 1; }

# 渲染结果必须带 alpha 通道(图标四角是透明的); 若渲染成白色错误页, 该检查会失败。
case "$(sips -g hasAlpha "$BASE_PNG" | tail -1)" in
    *yes*) ;;
    *) echo "错误: $BASE_PNG 没有 alpha 通道, SVG 可能未正确渲染" >&2; exit 1 ;;
esac

# ---- 2. 生成 .iconset 各尺寸 ----------------------------------------
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
cp "$BASE_PNG" "$ICONSET/icon_512x512@2x.png"   # 1024x1024(CoreGraphicsRetina)

# 每个 1x 尺寸同时产出对应 2x 尺寸; 覆盖 16/32/64/128/256/512/1024 全部像素规格
for pt in 16 32 128 256 512; do
    sips -z "$pt" "$pt" "$BASE_PNG" --out "$ICONSET/icon_${pt}x${pt}.png" >/dev/null
    px=$((pt * 2))
    sips -z "$px" "$px" "$BASE_PNG" --out "$ICONSET/icon_${pt}x${pt}@2x.png" >/dev/null
done

echo "生成尺寸:"
ls -1 "$ICONSET" | sed 's/^/  /'

# ---- 3. 打包 .icns ---------------------------------------------------
iconutil -c icns "$ICONSET" -o "$OUT"
echo "完成: $OUT"
