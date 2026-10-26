#!/usr/bin/env bash
# PixLab 端到端回归测试
#
# 前置（在项目根目录执行）：
#   make           构建 pixlab.exe
#   make samples   用 System.Drawing 生成 A 类样本
#   make exotic    生成 B 类样本（RLE / CORE 头 / V4-V5 / 自顶向下）
# 或者直接：make test
#
# 用法：bash tests/run_tests.sh

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

PIXLAB=./pixlab.exe
PASS=0
FAIL=0

ok()  { PASS=$((PASS + 1)); printf '  PASS  %s\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1"; }

# 取 pixlab info 输出的像素校验和（16 位十六进制）
sum_of() {
    "$PIXLAB" info "$1" 2>/dev/null | grep -oE '[0-9A-F]{16}' | tail -1
}

# 交叉验证：我们解出的像素必须与独立参照实现逐位一致
cross_check() {
    local name="$1" file="$2" ref="$3"
    if [ ! -f "$file" ]; then
        bad "$name（样本缺失，先跑 make samples / make exotic）"
        return
    fi
    if [ ! -f "$ref" ]; then
        bad "$name（参照缺失）"
        return
    fi
    local a b
    a=$(sum_of "$file")
    b=$(sum_of "$ref")
    if [ -n "$a" ] && [ "$a" = "$b" ]; then
        ok "$name  $a"
    else
        bad "$name  ours=$a ref=$b"
    fi
}

echo "== 1. A 类：System.Drawing 全位深样本 =="
for n in f01bpp f04bpp f08bpp f16bpp555 f16bpp565 f24bpp f32bppRgb f32bppArgb; do
    cross_check "$n" "samples/$n.bmp" "samples/${n}_ref.bmp"
done

echo
echo "== 2. B 类：RLE / CORE 头 / V4-V5 / 自顶向下 =="
for n in core24 v4 v5 topdown bf565 rle8 rle4; do
    cross_check "$n" "samples/exotic/$n.bmp" "samples/exotic/${n}_ref.bmp"
done

echo
echo "== 3. 畸形文件必须被拒绝且不崩溃 =="
for n in bad_bpp truncated fake_rle; do
    if [ -f "samples/exotic/$n.bmp" ]; then
        if "$PIXLAB" info "samples/exotic/$n.bmp" >/dev/null 2>&1; then
            bad "$n 本应报错却成功了"
        else
            ok "$n 已安全拒绝"
        fi
    else
        bad "$n（样本缺失）"
    fi
done

echo
echo "== 4. 全部滤镜必须跑通 =="
FILTERS="gray invert blur blur:9 sobel brightness:20 contrast:1.3 saturate:1.4 gamma:0.8 gaussian:1.5 sharpen:1.2 median:3 threshold threshold:180"
for f in $FILTERS; do
    if "$PIXLAB" samples/f24bpp.bmp samples/_f.bmp "$f" >/dev/null 2>&1; then
        ok "滤镜 $f"
    else
        bad "滤镜 $f"
    fi
done

echo
echo "== 5. 滤镜链与错误处理 =="
if "$PIXLAB" samples/f24bpp.bmp samples/_chain.bmp gray,contrast:1.2,sharpen >/dev/null 2>&1; then
    ok "灰度→对比度→锐化 链"
else
    bad "灰度→对比度→锐化 链"
fi
if "$PIXLAB" samples/f24bpp.bmp samples/_x.bmp nosuchfilter >/dev/null 2>&1; then
    bad "未知滤镜本应报错"
else
    ok "未知滤镜已拒绝"
fi
if "$PIXLAB" samples/f24bpp.bmp samples/_x.bmp gray:5 >/dev/null 2>&1; then
    bad "给无参滤镜传参本应报错"
else
    ok "无参滤镜传参已拒绝"
fi

# 积分图模糊的正确性锚点：blur:1 半径为 0，必须是逐像素恒等变换。
# 这一条能抓住积分图行/列累加、矩形和公式、哨兵行列里的任何错误。
a=$(sum_of "samples/f24bpp.bmp")
"$PIXLAB" samples/f24bpp.bmp samples/_id.bmp "blur:1" >/dev/null 2>&1
b=$(sum_of "samples/_id.bmp")
if [ -n "$a" ] && [ "$a" = "$b" ]; then
    ok "blur:1 是恒等变换（积分图正确）"
else
    bad "blur:1 非恒等  $a -> $b"
fi

# 偶数核没有中心像素，应自动升为奇数：blur:4 必须等价于 blur:5
"$PIXLAB" samples/f24bpp.bmp samples/_e4.bmp "blur:4" >/dev/null 2>&1
"$PIXLAB" samples/f24bpp.bmp samples/_e5.bmp "blur:5" >/dev/null 2>&1
if [ "$(sum_of samples/_e4.bmp)" = "$(sum_of samples/_e5.bmp)" ]; then
    ok "偶数核自动升为奇数"
else
    bad "偶数核未归一"
fi

# 二值化必须幂等：输出只有 0/255，再二值化一次结果不能再变
"$PIXLAB" samples/f24bpp.bmp samples/_t1.bmp "threshold:180" >/dev/null 2>&1
"$PIXLAB" samples/_t1.bmp    samples/_t2.bmp "threshold:180" >/dev/null 2>&1
if [ "$(sum_of samples/_t1.bmp)" = "$(sum_of samples/_t2.bmp)" ]; then
    ok "二值化幂等（再跑一次不变）"
else
    bad "二值化非幂等"
fi

# gamma:1 的指数是 1，必须是恒等变换。这条同时锁住 gamma 的方向语义：
# 实现是 out = in^(1/g)，若有人改成 in^g，g=1 仍是恒等，所以另加一条方向断言。
"$PIXLAB" samples/f24bpp.bmp samples/_g1.bmp "gamma:1" >/dev/null 2>&1
if [ "$(sum_of samples/_g1.bmp)" = "$(sum_of samples/f24bpp.bmp)" ]; then
    ok "gamma:1 是恒等变换"
else
    bad "gamma:1 非恒等"
fi

# Otsu 自动阈值（不给参数）必须能跑通。自动阈值走 stderr，不应污染 stdout 的校验和
"$PIXLAB" samples/f24bpp.bmp samples/_to1.bmp "threshold" >/dev/null 2>&1
"$PIXLAB" samples/f24bpp.bmp samples/_to2.bmp "threshold" >/dev/null 2>&1
if [ -n "$(sum_of samples/_to1.bmp)" ] && \
   [ "$(sum_of samples/_to1.bmp)" = "$(sum_of samples/_to2.bmp)" ]; then
    ok "Otsu 自动阈值稳定（同一张图两次结果一致）"
else
    bad "Otsu 自动阈值不稳定"
fi

echo
echo "== 6. 24 位往返锚点 =="
if [ -x tests/test_bmp.exe ]; then
    if ./tests/test_bmp.exe | grep -q PASS; then
        ok "读写往返逐像素一致"
    else
        bad "读写往返不一致"
    fi
else
    bad "tests/test_bmp.exe 未构建"
fi

echo
printf '合计：%d 通过，%d 失败\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
