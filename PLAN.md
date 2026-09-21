# PixLab 项目计划

这是一个用 C 语言实现的 BMP 图像处理练习项目，从"内存中的图像处理"逐步过渡到"真实 BMP 文件的读写和滤镜处理"。

最终形态：

```bash
pixlab input.bmp output.bmp gray
pixlab input.bmp output.bmp invert
pixlab input.bmp output.bmp blur
pixlab input.bmp output.bmp sobel
pixlab input.bmp output.bmp gray,blur,sobel
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
| M9 原样写回（锚点） | ✅ | `fc /b` 字节级一致 |
| M10 接入滤镜 | ✅ | `pixlab in.bmp out.bmp gray` |

### 阶段三：命令行接口

| 里程碑 | 状态 | 交付 |
|---|---|---|
| M12 参数解析 | ✅ | `argc/argv` 解析、help 信息 |
| M13 多滤镜串联 | ✅ | `gray,blur,sobel` 链式处理 |

---

## 代码结构

```text
image_processing_tool/
├── main.c              # CLI 入口：参数解析 + 滤镜链调度
├── image.h             # Image 结构体、BMP 读写声明
├── image.c             # 图像容器、bmp_load、bmp_save
├── filter.h            # 滤镜函数声明
├── filter.c            # invert / gray / blur / sobel
├── test.c              # 独立测试入口（BMP 原样写回锚点）
├── generate_bmp.py     # BMP 生成脚本
├── test.bmp            # 测试样例
├── PLAN.md             # 本文件
└── README.md           # 项目说明
```

---

## 构建方式

```bash
gcc -Wall -Wextra -g image.c filter.c main.c -o pixlab
```

或测试锚点：

```bash
gcc -Wall -Wextra -g image.c filter.c test.c -o test.exe
```

---

## 验收记录

- M4 模糊：`100 84 144 94 127 118 177 194`
- M5 Sobel：`0 0 0 255 255 0 0 0`
- M6a Image：`8 8 1` 字符画正确
- M8 灰度化：`76 149 28 255 0 128 226 178`
- M9 锚点：`fc /b test.bmp out.bmp` → `no differences encountered`
