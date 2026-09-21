#include "filter.h"
#include "image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char* prog) {
    printf("usage: %s input.bmp output.bmp <filter>[,<filter>...]\n", prog);
    printf("\n");
    printf("filters:\n");
    printf("  gray      RGB -> 灰度\n");
    printf("  invert    逐通道取反（负片）\n");
    printf("  blur      3x3 均值模糊（先灰度）\n");
    printf("  sobel     Sobel 边缘检测（先灰度）\n");
    printf("\n");
    printf("examples:\n");
    printf("  %s in.bmp out.bmp gray\n", prog);
    printf("  %s in.bmp out.bmp gray,blur\n", prog);
    printf("  %s in.bmp out.bmp gray,blur,sobel\n", prog);
    printf("  %s in.bmp out.bmp invert\n", prog);
}

// 把 src 的像素数据整块复制到一张新图里
static Image* image_clone(const Image* src) {
    Image* dst = image_new(src->width, src->height, src->channels);
    if (!dst) return NULL;
    size_t n = (size_t)src->width * src->height * src->channels;
    memcpy(dst->data, src->data, n);
    return dst;
}

// 对一张图应用单个滤镜，返回处理后的图（可能是新图）
// 调用者负责释放返回的图；失败返回 NULL
static Image* apply_one(const Image* src, const char* name) {
    if (strcmp(name, "gray") == 0) {
        Image* dst = image_new(src->width, src->height, 1);
        if (!dst) return NULL;
        to_gray(src, dst);
        return dst;
    }

    if (strcmp(name, "invert") == 0) {
        // 负片：逐通道取反，保留原通道数
        Image* dst = image_clone(src);
        if (!dst) return NULL;
        size_t n = (size_t)src->width * src->height * src->channels;
        memcpy(dst->data, src->data, n);
        invert_map(dst);
        return dst;
    }

    if (strcmp(name, "blur") == 0 || strcmp(name, "sobel") == 0) {
        // blur / sobel 只接受单通道图
        const Image* gray_src = src;
        Image* gray_tmp = NULL;
        if (src->channels != 1) {
            gray_tmp = image_new(src->width, src->height, 1);
            if (!gray_tmp) return NULL;
            to_gray(src, gray_tmp);
            gray_src = gray_tmp;
        }
        Image* dst = image_new(src->width, src->height, 1);
        if (!dst) {
            image_free(gray_tmp);
            return NULL;
        }
        if (strcmp(name, "blur") == 0) {
            blur3x3(gray_src, dst);
        } else {
            sobel(gray_src, dst);
        }
        image_free(gray_tmp);
        return dst;
    }

    return NULL; // 未知滤镜
}

int main(int argc, char** argv) {
    // --help / -h
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(argv[0]);
        return 0;
    }

    if (argc != 4) {
        usage(argv[0]);
        return 1;
    }

    const char* in_path = argv[1];
    const char* out_path = argv[2];
    const char* chain = argv[3];

    Image* cur = bmp_load(in_path);
    if (!cur) {
        fprintf(stderr, "failed to load: %s\n", in_path);
        return 1;
    }
    printf("loaded : %s (%dx%d, %d channels)\n",
           in_path, cur->width, cur->height, cur->channels);

    // 按逗号拆分滤镜名，逐个作用
    char* chain_copy = strdup(chain);
    if (!chain_copy) {
        image_free(cur);
        return 1;
    }

    char* saveptr = NULL;
    char* name = strtok_r(chain_copy, ",", &saveptr);
    while (name) {
        printf("apply  : %s\n", name);

        Image* next = apply_one(cur, name);
        if (!next) {
            fprintf(stderr, "unknown filter or OOM: %s\n", name);
            free(chain_copy);
            image_free(cur);
            return 1;
        }

        // 释放上一步的图，换上新图
        image_free(cur);
        cur = next;

        name = strtok_r(NULL, ",", &saveptr);
    }
    free(chain_copy);

    int rc = bmp_save(out_path, cur);
    if (rc != 0) {
        fprintf(stderr, "failed to save: %s\n", out_path);
        image_free(cur);
        return 1;
    }

    printf("saved  : %s (%s)\n", out_path, chain);
    image_free(cur);
    return 0;
}