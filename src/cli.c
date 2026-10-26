#include "cli.h"
#include "bmp.h"
#include "filter.h"
#include "image.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ============================ 小工具 ============================ */

/* 在缓冲里取下一段（原地改分隔符为 '\0'）。
   不用 strtok_r/strdup —— 那是 POSIX，-std=c99 下未必有声明。 */
static char* next_token(char** p, char sep) {
    char* s = *p;
    if (!s || !*s) return NULL;
    while (*s == sep) s++; // 容忍多余的分隔符
    if (!*s) {
        *p = NULL;
        return NULL;
    }
    char* start = s;
    while (*s && *s != sep) s++;
    if (*s == sep) {
        *s = '\0';
        *p = s + 1;
    } else {
        *p = NULL;
    }
    return start;
}

/* FNV-1a：给解码结果的像素算个指纹，用于跨格式比对是否解码一致。
   按像素的 (R,G,B) 三元组累积而不是按字节 —— 灰度调色板图会被我们解码成
   1 通道、被参照实现转成 3 通道，只有归一化到 RGB 两者才可比。 */
static unsigned long long pixel_checksum(const Image* img) {
    unsigned long long h = 1469598103934665603ULL;
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            const unsigned char* p = image_at(img, y, x);
            unsigned char r, g, b;
            if (img->channels == 1) {
                r = g = b = p[0];
            } else {
                r = p[0];
                g = p[1];
                b = p[2];
            }
            h ^= r;
            h *= 1099511628211ULL;
            h ^= g;
            h *= 1099511628211ULL;
            h ^= b;
            h *= 1099511628211ULL;
        }
    }
    return h;
}

static void usage(const char* prog) {
    printf("PixLab — BMP 图像处理工具\n\n");
    printf("用法:\n");
    printf("  %s <输入.bmp> <输出.bmp> <滤镜链>\n", prog);
    printf("  %s info <文件.bmp>\n", prog);
    printf("  %s --list\n", prog);
    printf("  %s --help\n\n", prog);

    printf("滤镜链用逗号分隔，参数写在冒号后：\n");
    printf("  %s in.bmp out.bmp gray\n", prog);
    printf("  %s in.bmp out.bmp brightness:20,contrast:1.3\n", prog);
    printf("  %s in.bmp out.bmp gamma:0.8,saturate:1.4,sharpen:1.2\n\n", prog);

    printf("输入支持 1/4/8/16/24/32 位、调色板、RLE 压缩的 BMP，\n");
    printf("输出固定为 24 位无压缩 BMP。\n\n");
    printf("用 '%s --list' 查看全部滤镜。\n", prog);
}

static void list_filters(void) {
    printf("可用滤镜（写法：名字 或 名字:参数）\n\n");
    for (const Filter* f = filter_table(); f->name; f++) {
        if (f->has_arg)
            printf("  %-11s %s（默认 %.3g）\n", f->name, f->help, f->def_arg);
        else
            printf("  %-11s %s\n", f->name, f->help);
    }
}

/* ============================ info 子命令 ============================ */

static int cmd_info(const char* path) {
    BmpInfo info;
    BmpError e = bmp_probe(path, &info);
    if (e != BMP_OK) {
        fprintf(stderr, "无法读取 %s：%s\n", path, bmp_strerror(e));
        return 1;
    }

    printf("文件        : %s\n", path);
    printf("文件大小    : %lu 字节\n", (unsigned long)info.file_size);
    printf("头类型      : %s（biSize=%u）\n", bmp_header_name(info.header_size),
           info.header_size);
    printf("尺寸        : %d x %d (%s)\n", info.width,
           info.height < 0 ? -info.height : info.height,
           info.top_down ? "自顶向下" : "自底向上");
    printf("位深        : %u 位\n", info.bpp);
    printf("压缩        : %s\n", bmp_compression_name(info.compression));
    printf("像素偏移    : %u\n", info.off_bits);
    printf("行宽(stride): %lu 字节\n", (unsigned long)info.stride);
    printf("biSizeImage : %u%s\n", info.size_image,
           info.size_image == 0 ? "（为 0，按 stride×height 推算）" : "");
    printf("分辨率      : %d x %d ppm\n", info.xppm, info.yppm);
    printf("调色板      : %d 项\n", info.palette_entries);
    if (info.has_masks)
        printf("位掩码      : R=%08X G=%08X B=%08X%s\n", info.mask_r, info.mask_g,
               info.mask_b, info.mask_a ? " (含 alpha)" : "");
    else
        printf("位掩码      : 无\n");

    BmpError le;
    Image* img = bmp_load_ex(path, &le);
    if (!img) {
        fprintf(stderr, "\n解码失败：%s\n", bmp_strerror(le));
        return 1;
    }
    printf("\n解码结果    : %d x %d, %d 通道%s\n", img->width, img->height,
           img->channels, img->channels == 1 ? "（灰度）" : "（RGB）");
    printf("像素校验和  : %016llX\n", pixel_checksum(img));
    image_free(img);
    return 0;
}

/* ============================ 主流程 ============================ */

/* 依次施加滤镜链。成功时把最终图写入 *out，失败返回非 0（并已释放中间图）。 */
static int run_chain(Image* img, char* chain, const char* in_path, Image** out) {
    Image* cur = img;
    char* p = chain;
    char* tok;

    while ((tok = next_token(&p, ',')) != NULL) {
        // 拆出 "名字:参数"
        char* colon = strchr(tok, ':');
        double arg = 0.0;
        if (colon) {
            *colon = '\0';
            char* endp = NULL;
            arg = strtod(colon + 1, &endp);
            if (endp == colon + 1 || (endp && *endp != '\0')) {
                fprintf(stderr, "滤镜 %s 的参数无法解析：'%s'\n", tok, colon + 1);
                image_free(cur);
                return 1;
            }
        }

        const Filter* f = filter_find(tok);
        if (!f) {
            fprintf(stderr, "未知滤镜：'%s'（用 --list 查看全部）\n", tok);
            image_free(cur);
            return 1;
        }
        if (colon && !f->has_arg) {
            fprintf(stderr, "滤镜 %s 不接受参数\n", f->name);
            image_free(cur);
            return 1;
        }

        double use = colon ? arg : f->def_arg;
        Image* next = f->apply(cur, use);
        if (!next) {
            fprintf(stderr, "滤镜 %s 处理失败（内存不足或参数越界）\n", f->name);
            image_free(cur);
            return 1;
        }
        image_free(cur);
        cur = next;
        printf("  已应用 %s%s\n", f->name, colon ? "" : "");
    }

    (void)in_path;
    *out = cur;
    return 0;
}

int cli_run(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // 否则中文提示在 Windows 控制台会乱码
#endif

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "--list") == 0) {
        list_filters();
        return 0;
    }
    if (strcmp(argv[1], "info") == 0) {
        if (argc != 3) {
            fprintf(stderr, "用法：%s info <文件.bmp>\n", argv[0]);
            return 1;
        }
        return cmd_info(argv[2]);
    }

    if (argc != 4) {
        usage(argv[0]);
        return 1;
    }

    const char* in_path = argv[1];
    const char* out_path = argv[2];

    BmpError e;
    Image* img = bmp_load_ex(in_path, &e);
    if (!img) {
        fprintf(stderr, "无法读取 %s：%s\n", in_path, bmp_strerror(e));
        return 1;
    }
    printf("读入 %s：%d x %d, %d 通道%s\n", in_path, img->width, img->height,
           img->channels, img->channels == 1 ? "（灰度）" : "（RGB）");

    Image* result = NULL;
    if (run_chain(img, argv[3], in_path, &result) != 0) return 1;

    if (bmp_save(out_path, result) != 0) {
        fprintf(stderr, "写出失败：%s\n", out_path);
        image_free(result);
        return 1;
    }
    printf("写出 %s：%d x %d\n", out_path, result->width, result->height);
    image_free(result);
    return 0;
}
