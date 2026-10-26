# PixLab

PixLab 是一个用纯 C99 手写实现的 BMP 图像处理命令行工具，**零第三方依赖**。

它可以直接处理日常照片导出的 BMP —— 包括 Windows 画图 / Photoshop / 相机软件常见的
32 位、8 位调色板、BITMAPV4/V5 头等格式，而不只是教科书上的 24 位无压缩图。

```bash
pixlab 我的照片.bmp 输出.bmp gamma:0.8,saturate:1.4,sharpen:1.2
```

---

## 构建

需要 gcc（本环境为 mingw64 gcc 15.3.0）与 make。

```bash
make            # 生成 pixlab.exe
```

编译选项固定为 `-std=c99 -Wall -Wextra -O2 -g`，**零警告**。

---

## 用法

```bash
pixlab <输入.bmp> <输出.bmp> <滤镜链>   # 处理图像
pixlab info <文件.bmp>                  # 打印头部全部字段 + 像素校验和
pixlab --list                           # 列出全部滤镜
pixlab --help                           # 帮助
```

滤镜链用**逗号分隔**，滤镜参数写在**冒号后**（逗号已被占用为分隔符）：

```bash
pixlab in.bmp out.bmp gray
pixlab in.bmp out.bmp brightness:20,contrast:1.3
pixlab in.bmp out.bmp gamma:1.8,saturate:1.4,sharpen:1.2
pixlab in.bmp out.bmp gaussian:1.5,sharpen:1.0
pixlab in.bmp out.bmp median:3,contrast:1.2
pixlab in.bmp out.bmp blur:9
pixlab in.bmp out.bmp threshold        # 自动 Otsu 阈值
pixlab in.bmp out.bmp threshold:200    # 手动指定阈值
```

> `blur` 默认是 3×3（半径仅 1 像素）。在 2000 万像素的照片上这几乎看不出效果 ——
> **想要明显的模糊请加大核**，例如 `blur:31`。`gaussian:σ` 也能模糊，但半径 = 3σ，
> 且耗时随核增大（可分离卷积仍是 O(k)），`blur` 则无论多大都是 O(1)。

> **`threshold` 和 `contrast` 的区别**：`contrast` 把灰阶**拉开**（浅灰的网格只是变淡），
> `threshold` 把灰阶**坍缩**成两层（网格彻底消失）。处理电路图 / 文档 / 扫描件时，
> 想要"背景完全干净"就用 `threshold`。
>
> 自动 Otsu 对**双峰直方图**（纯白底 + 黑字）很准；但图里若有彩色内容，直方图会多
> 出一个峰，Otsu 算出的阈值会偏低（笔画偏瘦）。这时手动调高一些更好看。自动算出的
> 阈值会打印到 **stderr**，方便据此手调。
>
> 输出恒为单通道（只有 0 和 255 两个值）。

### 支持的滤镜

| 滤镜 | 参数 | 说明 |
|---|---|---|
| `gray` | — | RGB → 灰度（BT.601 亮度） |
| `invert` | — | 负片，逐通道取反 |
| `blur` | n | n×n 均值模糊，积分图实现，n 任意大都是 O(1)（默认 3） |
| `sobel` | — | Sobel 边缘检测（先转灰度） |
| `brightness` | ±n | 每通道加 n，例 `brightness:20` |
| `contrast` | f | `(v-128)*f+128`，f=1 不变，f>1 增强 |
| `saturate` | f | `luma + (v-luma)*f`，f=0 全灰，f=1 不变 |
| `gamma` | g | 伽马校正 `out = in^(1/g)`，**g > 1 提亮、g < 1 压暗** |
| `gaussian` | σ | 可分离高斯模糊，半径 `ceil(3σ)` |
| `sharpen` | amount | USM 锐化：`原图 + amount × (原图 − 高斯)` |
| `median` | k | k×k 中值去噪（k 为奇数，3..9） |
| `threshold` | t | 二值化：亮度 > t 变纯白、否则纯黑；不给参数则自动用 Otsu 算 t |

---

## 支持的 BMP 格式

解码器覆盖范围如下，全部手写实现，不依赖任何库：

| 维度 | 覆盖范围 |
|---|---|
| DIB 头 | CORE(12) / INFO(40) / V2(52) / V3(56) / OS2v2(64) / V4(108) / V5(124) |
| 位深 | 1、4、8（调色板）、16（任意掩码，含 555/565）、24、32 |
| 压缩 | BI_RGB(0)、BI_RLE8(1)、BI_RLE4(2)、BI_BITFIELDS(3)、BI_ALPHABITFIELDS(6) |
| 方向 | `biHeight > 0` 自底向上 / `< 0` 自顶向下 |
| 调色板 | CORE 头 3 字节 BGR 表项、INFO 头 4 字节 BGRX，`biClrUsed == 0` 时按 `2^bpp` |
| 位掩码 | 头内嵌（biSize ≥ 52）或紧随 40 字节头之后，16bpp 从掩码实际推导 |
| 未知 biSize | 41–51、65–107 等宽容处理：≥40 时按 INFO 布局读前 40 字节 |

**明确的报错而非静默失败**。不支持的格式会给出具体原因，例如：

```text
无法读取 x.bmp：不支持的位深 48
解码失败：RLE 压缩数据损坏
```

其中 BI_JPEG(4) / BI_PNG(5) 会单独提示（这两种 BMP 内嵌的是别的编码格式，
需要先转存为无压缩 BMP）。

### 输出格式

`bmp_save` 恒定输出 **40 字节头 + 24bpp + BI_RGB + 自底向上**，`biSizeImage` 按
`stride × height` 重算，保留原图的 `xppm/yppm`（物理分辨率）。

> **注意**：因此「字节级原样回写」不再是通用保证。规范 24bpp 输入仍能字节一致
> （回归测试中有锚点），但 32 位 / 调色板 / RLE 输入会被规范化输出。

### 两个刻意的设计取舍

1. **32 位 BMP 的 alpha 一律丢弃，不做透明合成**。`BI_RGB` 的 32bpp 第 4 字节按规范
   就是「保留位」，大量历史 BMP 里是 0 或垃圾数据；若按 alpha 与白底合成，整张图会
   被洗成白色。
2. **RLE run 跨行视为损坏并报错**。规范禁止 run 跨越行尾。若静默截断，一个损坏文件
   会解出一张「看起来正常」的垃圾图，用户无从察觉。

---

## 测试

```bash
make test       # 完整回归（自动生成样本 + 跑 33 项断言）
```

单独生成样本：

```bash
make samples    # A 类：System.Drawing 生成全位深样本 + 24 位参照
make exotic     # B 类：手工构造 RLE / CORE 头 / V4-V5 / 自顶向下
```

### 验证思路

不自己给自己打分。每个样本都用 **Windows 自带的 `System.Drawing`（GDI+）** 独立读一遍
并转存成 24 位基准，再比对我们解出的像素校验和：

```bash
pixlab info samples/f32bppArgb.bmp      # 我们的解码器
pixlab info samples/f32bppArgb_ref.bmp  # GDI+ 转出的 24 位基准
# 两者校验和必须一致
```

| 类别 | 样本 | 说明 |
|---|---|---|
| A 类 | f01bpp / f04bpp / f08bpp / f16bpp555 / f16bpp565 / f24bpp / f32bppRgb / f32bppArgb | `System.Drawing` 可生成，规范格式 |
| B 类 | core24 / v4 / v5 / topdown / bf565 / rle8 / rle4 | `System.Drawing` 生成不了，由 `tests/make_exotic.c` 按字节拼出 |
| 畸形 | bad_bpp / truncated / fake_rle | 必须清晰报错，不崩溃、不越界 |

当前状态：**40 通过，0 失败**。

---

## 代码结构

```text
image_processing_tool/
├── Makefile              # all / samples / exotic / test / clean
├── src/
│   ├── main.c            # 入口，仅调用 cli_run
│   ├── cli.h  cli.c      # 参数解析、滤镜链调度、info / --list / --help
│   ├── image.h image.c   # Image 容器（一维缓冲 + w/h/channels）
│   ├── bmp.h  bmp.c      # 全格式 BMP 解码 / 编码（核心）
│   ├── filter.h filter.c # 注册表驱动的滤镜
│   └── util.h util.c     # clamp、luma、溢出安全乘法、整文件读取
├── tests/
│   ├── gen_samples.ps1   # A 类样本生成（System.Drawing）
│   ├── gen_refs.ps1      # 为任意目录的 BMP 生成 24 位参照
│   ├── make_exotic.c     # B 类样本手工构造
│   ├── test_bmp.c        # 24 位往返锚点
│   ├── run_tests.sh      # 端到端回归
│   └── fixtures/test.bmp # 往返锚点用的固定样本
└── samples/              # 样本产出（已 gitignore）
```

### 架构要点

- **整文件读入 + 有界游标**：`BmpReader` 一次性读入文件后带边界检查地解析。
  RLE 解码、`bfOffBits` 校验、文件长度校验在有界内存视图下不可能越界。
  代价是峰值内存翻倍（4000×3000 照片约 72MB），可接受。
- **统一内部格式**：解码后一律展开为 3 通道 RGB；**调色板为纯灰阶时输出 1 通道**
  （内存减为 1/3，且能直接字符画）。判定基于实际被使用的表项。
- **滤镜注册表**：新增滤镜 = 在 `filter.c` 的表里加一行，CLI 无需改动。
- **安全上限**：`PIXLAB_MAX_PIXELS`（1 亿像素）、`PIXLAB_MAX_FILE`（256MB），
  所有尺寸计算走 uint64 中间量，先校验后分配。

---

## 已知限制

- **不做批处理**。单文件进出，批量交给 shell 循环：
  ```bash
  for f in *.bmp; do ./pixlab.exe "$f" "out/$f" gray; done
  ```
- **输出固定 24 位无压缩**，不保留输入的位深与压缩方式（见上文说明）。
- **`tests/generate_bmp.py` 是遗留脚本**，本环境没有 python，跑不了。它生成的
  `tests/fixtures/test.bmp` 仍被往返测试使用；脚本本身保留作为手工构造 BMP 字节的参考。

---

## 适用场景

- 学习 C 语言与内存管理、二进制文件格式解析
- 理解图像处理基础算法（卷积、灰度、边缘检测、USM、中值滤波）
- 批量为老照片做基础增强（提亮、加对比、去噪、锐化）
