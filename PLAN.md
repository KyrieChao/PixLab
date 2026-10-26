# PixLab 项目计划

这是一个用 C 语言实现的 BMP 图像处理项目。前三阶段从"内存中的图像处理"逐步过渡到
"真实 BMP 文件的读写和滤镜处理"；**阶段四**把它从一个只认 24 位无压缩图的练习程序，
改造成真正能处理日常照片导出的 BMP 的完整工具。

最终形态：

```bash
pixlab input.bmp output.bmp gray
pixlab input.bmp output.bmp brightness:20,contrast:1.3
pixlab input.bmp output.bmp gamma:0.8,saturate:1.4,sharpen:1.2
pixlab info photo.bmp
pixlab --list
```

---

## 里程碑状态（全部完成）

### 阶段一：图像算法（内存中操作像素）

| 里程碑 | 状态 | 交付 |
|---|---|---|
| M1 图像表示与字符显示 | ✅ | `image_print` |
| M2 负片处理 | ✅ | `invert` |
| M3 灰度化 | ✅ | `rgb_to_luma`、`to_gray` |
| M4 均值模糊 | ✅ | `blur3x3` |
| M5 Sobel 边缘检测 | ✅ | `sobel` |
| M6a Image 容器 | ✅ | `image_new`、`image_free`、`image_at` |
| M6b 滤镜迁到 Image* | ✅ | 全部改用 Image 结构体 |
| M6c 拆文件 | ✅ | `image.h/c`、`filter.h/c`、`main.c` |

### 阶段二：BMP 文件读写

| 里程碑 | 状态 | 交付 |
|---|---|---|
| M7 读取 BMP 文件头 | ✅ | 校验文件类型、位深、压缩方式 |
| M8 读取像素数据 | ✅ | 处理 padding、BGR→RGB、自底向上 |
| M9 原样写回（锚点） | ✅ | `fc /b` 字节级一致（**仅限规范 24bpp，见阶段四说明**） |
| M10 接入滤镜 | ✅ | `pixlab in.bmp out.bmp gray` |

### 阶段三：命令行接口

| 里程碑 | 状态 | 交付 |
|---|---|---|
| M12 参数解析 | ✅ | `argc/argv` 解析、help 信息 |
| M13 多滤镜串联 | ✅ | `gray,blur,sobel` 链式处理 |

### 阶段四：全格式兼容与工具化

**动因**：阶段二的三行硬编码校验把绝大多数真实 BMP 挡在门外。

```c
/* 旧 image.c:94 */
if (biCount != 24 || biCompression != 0 || biWidth <= 0 || biSize != 40 || ...) return NULL;
```

实测：用 Windows 自带 `System.Drawing` 导出一张普通位图，`biBitCount = 32`、
`biSizeImage = 0`，旧程序直接 `failed to load` 且**不告诉任何原因**。

| 里程碑 | 状态 | 交付 |
|---|---|---|
| M14 目录重组 | ✅ | `src/` + `tests/`，新增 Makefile，`git mv` 保留历史 |
| M15 全格式解码器 | ✅ | `src/bmp.c`：7 种 DIB 头 × 6 种位深 × 4 种压缩 |
| M16 可解释错误 | ✅ | `BmpError` 错误码 + `bmp_strerror`，取代静默 NULL |
| M17 位掩码归一化 | ✅ | 16bpp 从掩码推导，位复制缩放（非除法） |
| M18 RLE8 / RLE4 | ✅ | 先解索引平面，再复用调色板展开路径 |
| M19 编码策略调整 | ✅ | 恒定输出 24bpp + 40 字节头，`biSizeImage` 重算 |
| M20 滤镜注册表 + 7 个新滤镜 | ✅ | 表驱动，共 11 个滤镜 |
| M21 CLI 子命令 | ✅ | `info` / `--list` / `--help`，滤镜链错误定位 |
| M22 交叉验证测试 | ✅ | 以 GDI+ 为独立参照，33 项断言 |
| M23 文档重写 | ✅ | README / PLAN / .gitignore |
| M24 二值化 | ✅ | `threshold`：手动阈值 + Otsu 自动阈值 |

---

## 阶段四关键设计

### 格式覆盖

| 维度 | 覆盖范围 |
|---|---|
| DIB 头 | CORE(12) / INFO(40) / V2(52) / V3(56) / OS2v2(64) / V4(108) / V5(124) |
| 位深 | 1、4、8（调色板）、16（任意掩码）、24、32 |
| 压缩 | BI_RGB(0)、BI_RLE8(1)、BI_RLE4(2)、BI_BITFIELDS(3)、BI_ALPHABITFIELDS(6) |
| 方向 | `biHeight > 0` 自底向上 / `< 0` 自顶向下 |

未知 `biSize`（41–51、65–107 等）宽容处理：`>= 40` 时按 INFO 布局读前 40 字节。

### 架构：整文件读入 + 有界游标

不用 `fseek/fread` 流式解析，而是**一次性读入文件**再用带边界检查的游标
（`BmpReader`）。理由：RLE 解码、`bfOffBits` 校验、文件长度校验在有界内存视图下
最简单且不可能越界。代价是峰值内存翻倍（4000×3000 照片约 72MB），可接受。

### 统一内部格式

解码后一律展开为 **3 通道 RGB**；**调色板为纯灰阶时输出 1 通道**（内存减为 1/3，
且 `image_print` 能直接字符画）。判定基于**实际被使用的表项**，不把"存了 256 项但
只用 3 项"的图误判。

### 两个刻意的取舍

1. **32 位 alpha 一律丢弃，不合成**。`BI_RGB` 的 32bpp 第 4 字节按规范就是「保留位」；
   大量历史 BMP 的 alpha 是 0 或垃圾，白底合成会把整张图变白。
2. **RLE run 跨行报错**。规范禁止 run 跨越行尾；静默截断会让损坏文件解出
   「看起来正常」的垃圾图。

### 位掩码归一化

`build_mask()` 从任意掩码算 `shift`（尾零个数）与 `bits`（popcount），通吃 `0xF800`
型和 `0x00FF0000` 型。缩放用**位复制**而非除法 —— 5 位满量程 31→255 恰好命中且无
舍入偏差：`(v<<3)|(v>>2)`。**16bpp 必须从掩码实际推导，不能写死 555**（565 更常见）。

### 均值模糊用积分图

`blur:n` 支持任意 n×n 均值核，用积分图（summed area table）实现，每像素 O(1)，
与核大小无关 —— 直接卷积是 O(n²)，n=201 时慢 4489 倍。实测（1748×1082）：

| 写法 | 核 | 耗时 |
|---|---|---|
| `blur:3` | 3×3 | 0.61s |
| `blur:51` | 51×51 | 0.67s |
| `blur:201` | 201×201 | 0.56s |
| `gaussian:25` | 半径 75（可分离） | 2.03s |

边界按「有效区域截断」求平均，而非复制边缘像素 —— 后者在核很大时会让边框像素
被反复计权，边缘出现偏色。偶数核没有中心像素，统一向上取奇数。

`blur:1`（半径为 0）必须是逐像素恒等变换，这条被写进回归测试，当作积分图行/列
累加、矩形和公式、哨兵行列里的任何错误的锚点。

### 二值化与 Otsu 阈值

`threshold` 把灰阶**坍缩**成两层（0 / 255），与 `contrast` 的「拉开灰阶」有本质区别
—— 浅灰的网格背景会**彻底消失**而非仅仅变淡。输出恒为单通道。

不给参数时用**大津法（Otsu）**自动求阈值：在 0..255 上找使类间方差
`wB*wF*(mB-mF)²` 最大的那个点。乘上两堆的像素数，是为了防止「只有一个像素的堆」
因为均值差得远而胜出。

对双峰直方图（纯白底 + 黑字）Otsu 很准；但图里有彩色内容时直方图会多出峰，算出的
阈值偏低。实测那张 1748×1082 的电路图（含红/绿/蓝线条）Otsu 给出 **164**，而手动
**215** 的笔画更饱满。因此自动值会打印到 **stderr**，供用户据此手调。

自动阈值走 stderr 而非 stdout，是为了不污染 stdout 的像素校验和 —— 回归测试依赖
这一点来断言"同一张图两次自动阈值结果一致"。

### 内存与上限

```c
#define PIXLAB_MAX_PIXELS (100u * 1000u * 1000u)  /* 1 亿像素 */
#define PIXLAB_MAX_FILE   (256u * 1024u * 1024u)  /* 256MB */
```

所有尺寸计算用 `uint64_t` 中间量：先校验 `bfOffBits + stride*height <= file_len`，
再分配、再读。这能一次性挡住绝大多数畸形文件。

### ⚠️ 「字节级原样回写」的降级

阶段二 M9 宣传的「字节级原样回写」是建立在「只支持 24bpp 无压缩」这一前提上的，
靠 `BmpMeta` 保存 8 个原始头字段实现。**全格式支持后这个保证不再普遍成立**：

- 32 位输入若把原始 `biSizeImage` 写进 24 位输出，头字段自相矛盾
- 调色板 / RLE 输入根本无法以原编码方式回写

因此 `BmpMeta` 简化为 5 个字段（`xppm`、`yppm`、两个 reserved），`bmp_save` 恒定
输出 **40 字节头 + 24bpp + BI_RGB + 自底向上**，`biSizeImage` 按 `stride*height`
**重算**。

> 规范 24bpp 输入（如 `tests/fixtures/test.bmp`）仍能字节一致，回归测试保留了这个
> 锚点；但它已从「通用承诺」降级为「特定情况下的性质」。

---

## 验收记录（阶段四）

```
== 1. A 类：System.Drawing 全位深样本 ==
  PASS  f01bpp  66CE96CFF984DAC5
  PASS  f04bpp  425DADA9EC6100D1
  PASS  f08bpp  7E076E952BDC8494
  PASS  f16bpp555  B8F612F88A0B9BE7
  PASS  f16bpp565  C24854A501D86C20
  PASS  f24bpp  EBF69D8126A35EC2
  PASS  f32bppRgb  EBF69D8126A35EC2
  PASS  f32bppArgb  EBF69D8126A35EC2

== 2. B 类：RLE / CORE 头 / V4-V5 / 自顶向下 ==
  PASS  core24 / v4 / v5 / topdown 均为 9AB323396FC4B4A0
  PASS  bf565  E5113CAEA64C0E59
  PASS  rle8  871122A7A74210BB
  PASS  rle4  2A08996A89AC029B

== 3. 畸形文件 ==   bad_bpp / truncated / fake_rle 全部安全拒绝
== 4. 滤镜 ==      14/14 跑通
== 5. 滤镜链 ==    链式、未知滤镜、无参滤镜传参、blur:1 恒等、偶数核归一、
                   二值化幂等、Otsu 稳定   全部符合预期
== 6. 往返锚点 ==  逐像素一致

合计：40 通过，0 失败
```

真实尺寸照片端到端（1920×1080，6220854 字节）：

```
$ time ./pixlab.exe big1920.bmp out.bmp gray,contrast:1.2,sharpen
读入 big1920.bmp：1920 x 1080, 3 通道（RGB）
  已应用 gray / contrast / sharpen
写出 out.bmp：1920 x 1080
real    0m0.955s
```

值得注意的两点：

- `f24bpp` / `f32bppRgb` / `f32bppArgb` 三者校验和**完全相同**（`EBF69D8126A35EC2`）
  —— 证明 32 位 alpha 丢弃策略正确。
- `f16bpp555` / `f16bpp565` 与 GDI+ 逐位一致 —— 证明掩码解析与位复制缩放正确。

### 阶段一~三的历史验收（保留）

- M4 模糊：`100 84 144 94 127 118 177 194`
- M5 Sobel：`0 0 0 255 255 0 0 0`
- M6a Image：`8 8 1` 字符画正确
- M8 灰度化：`76 149 28 255 0 128 226 178`
- M9 锚点：`fc /b test.bmp out.bmp` → `no differences encountered`

---

## 构建方式

```bash
make            # 构建 pixlab.exe
make test       # 完整回归（生成样本 + 33 项断言）
make samples    # 仅生成 A 类样本
make exotic     # 仅生成 B 类样本
make clean
```

编译选项：`-std=c99 -Wall -Wextra -O2 -g`，零警告。

---

## 代码结构

```text
image_processing_tool/
├── Makefile
├── README.md
├── PLAN.md
├── src/
│   ├── main.c            # 入口，仅调用 cli_run
│   ├── cli.h  cli.c      # 参数解析、滤镜链调度、info / --list / --help
│   ├── image.h image.c   # Image 容器
│   ├── bmp.h  bmp.c      # 全格式 BMP 解码 / 编码（核心）
│   ├── filter.h filter.c # 注册表驱动的滤镜
│   └── util.h util.c     # clamp、luma、溢出安全乘法、整文件读取
├── tests/
│   ├── gen_samples.ps1   # A 类样本生成（System.Drawing）
│   ├── gen_refs.ps1      # 为任意目录的 BMP 生成 24 位参照
│   ├── make_exotic.c     # B 类样本手工构造
│   ├── test_bmp.c        # 24 位往返锚点
│   ├── run_tests.sh      # 端到端回归
│   ├── fixtures/test.bmp # 往返锚点固定样本
│   └── generate_bmp.py   # 遗留脚本（本环境无 python，仅作参考）
└── samples/              # 样本产出（已 gitignore）
```

---

## 验证方式

系统无 python，旧 `generate_bmp.py` 跑不了。改用 Windows 自带 `System.Drawing`
—— 已验证可用，且能作为**交叉验证的参照实现**。实测它支持 `Format1bppIndexed`、
`4bppIndexed`、`8bppIndexed`、`16bppRgb555`、`16bppRgb565`、`24bppRgb`、`32bppRgb`、
`32bppArgb`。

核心思路是**不自己给自己打分**：每个样本都用 GDI+ 独立读一遍并转存成 24 位基准，
再比对我们解出的像素校验和（FNV-1a，按 RGB 三元组累积，与通道数无关）。

- **A 类**：GDI+ 能生成，覆盖「平时照片」的绝大多数真实情况
- **B 类**：GDI+ 生成不了（RLE / CORE 头 / V4-V5 / 自顶向下），由 `make_exotic.c`
  按字节拼出，同样用 GDI+ 读回做参照
- **畸形类**：bad_bpp / truncated / fake_rle，必须清晰报错不崩溃

> 注：GDI+ 无法对索引格式位图创建 `Graphics`，生成索引位深样本必须用
> `Bitmap.Clone(rect, format)` 而非 `Graphics.FromImage`。

---

## 已知限制

- **不做批处理**，单文件进出，批量交给 shell 循环
- **输出固定 24 位无压缩**，不保留输入的位深与压缩方式
- BI_JPEG(4) / BI_PNG(5) 明确报错（需先转存为无压缩 BMP）
