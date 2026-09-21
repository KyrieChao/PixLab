# PixLab

PixLab 是一个用 C 语言实现的 BMP 图像处理命令行工具。

支持读取 24 位无压缩 BMP，执行灰度化、负片、模糊、边缘检测等操作，并输出处理后的 BMP 文件。

---

## 用法

```bash
pixlab input.bmp output.bmp <滤镜>[,<滤镜>...]
```

### 支持的滤镜

| 滤镜 | 说明 |
|---|---|
| `gray` | RGB → 灰度 |
| `invert` | 逐通道取反（负片） |
| `blur` | 3×3 均值模糊（自动先灰度） |
| `sobel` | Sobel 边缘检测（自动先灰度） |

### 示例

```bash
pixlab in.bmp out.bmp gray
pixlab in.bmp out.bmp invert
pixlab in.bmp out.bmp blur
pixlab in.bmp out.bmp sobel
pixlab in.bmp out.bmp gray,blur,sobel
```

---

## 构建

在项目目录下执行：

```bash
gcc -Wall -Wextra -g image.c filter.c main.c -o pixlab
```

测试 BMP 读写锚点：

```bash
gcc -Wall -Wextra -g image.c filter.c test.c -o test.exe
./test.exe
fc /b test.bmp out.bmp
```

---

## 代码结构

| 文件 | 内容 |
|---|---|
| `image.h/c` | `Image` 结构体、bmp_load、bmp_save |
| `filter.h/c` | invert、to_gray、blur3x3、sobel |
| `main.c` | CLI 入口、参数解析、滤镜链调度 |
| `test.c` | 独立测试入口（验证 BMP 原样写回） |

---

## 技术要点

- **Image 结构体**：一维连续缓冲 + width/height/channels，支持运行时尺寸。
- **BMP 读取**：逐字段 fread、校验 24 位无压缩、处理 padding、BGR→RGB、自底向上存储。
- **BMP 写回**：保留原始头字段（`BmpMeta`）原样回写、补填充字节、灰度输出为 24 位 BMP。
- **滤镜链**：自动灰度转换、`gray,blur,sobel` 顺序执行、每步释放前一张图。

---

## 适用场景

- 学习 C 语言与内存管理
- 理解图像处理基础算法（卷积、灰度、边缘检测）
- 练习结构体、指针、文件 IO
- BMP 格式入门

---

## 设计原则

- 算法与文件格式分开学，避免同时踩两个坑。
- 每个里程碑有明确验收标准，不跳步。
- 先验证小功能，再合并到更大的系统。
