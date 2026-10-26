#include "image.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Image* image_new(int width, int height, int channels) {
    if (width <= 0 || height <= 0) return NULL;
    if (channels != 1 && channels != 3) return NULL;

    size_t npix, size;
    if (!mul_ok((size_t)width, (size_t)height, &npix)) return NULL;
    if (!mul_ok(npix, (size_t)channels, &size)) return NULL;

    Image* img = malloc(sizeof(Image));
    if (!img) return NULL;
    img->data = malloc(size);
    if (!img->data) {
        free(img);
        return NULL;
    }
    img->width = width;
    img->height = height;
    img->channels = channels;
    memset(&img->bmp, 0, sizeof(img->bmp));
    return img;
}

Image* image_clone(const Image* src) {
    Image* dst = image_new(src->width, src->height, src->channels);
    if (!dst) return NULL;
    size_t npix, n;
    mul_ok((size_t)src->width, (size_t)src->height, &npix);
    mul_ok(npix, (size_t)src->channels, &n);
    memcpy(dst->data, src->data, n);
    dst->bmp = src->bmp; // 早先漏了这一步，滤镜链后会丢掉分辨率信息
    return dst;
}

void image_print(const Image* img) {
    static const char* lv[] = {"  ", "..", "::", "--", "==", "**", "##", "@@"};
    for (int i = 0; i < img->height; i++) {
        for (int j = 0; j < img->width; j++) {
            unsigned char* p = image_at(img, i, j);
            unsigned char l = (img->channels == 1) ? p[0] : luma_u8(p[0], p[1], p[2]);
            printf("%s", lv[l / 32]);
        }
        printf("\n");
    }
}

void image_free(Image* img) {
    if (!img) return;
    free(img->data);
    free(img);
}

unsigned char* image_at(const Image* img, int y, int x) {
    return img->data + ((size_t)x + (size_t)y * (size_t)img->width) * (size_t)img->channels;
}
