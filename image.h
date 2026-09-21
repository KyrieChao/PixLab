#ifndef IMAGE_H
#define IMAGE_H

#define MAX_CLAMP 255
#include <stdint.h>

// 保存 BMP 头里需要原样回写的字段
typedef struct {
    int valid; // 是否来自 bmp_load
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    int32_t biHeight_signed; // 保留原始符号
    uint32_t biSizeImage;
    uint32_t biXPelsPerMeter;
    uint32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BmpMeta;

typedef struct {
    int width, height, channels;
    unsigned char* data;
    BmpMeta bmp; // 非 BMP 来源时为 0
} Image;

Image* image_new(int width, int height, int channels);

void image_print(const Image* img);

void image_free(Image* img);

unsigned char* image_at(const Image* img, int y, int x);

Image* bmp_load(const char* path);

int bmp_save(const char* path, Image* img);

#endif