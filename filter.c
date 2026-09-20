#include "filter.h"

void invert(Image* img) {
    for (int i = 0; i < img->height; i++) {
        for (int j = 0; j < img->width; j++) {
            *(img->data + i * img->width + j) = 255 - *(img->data + i * img->width + j);
        }
    }
}

unsigned char rgb_to_luma(const unsigned char* rgb) {
    return (77 * rgb[0] + 150 * rgb[1] + 29 * rgb[2]) >> 8;
}

void to_gray(const Image* src, Image* dest) {
    for (int i = 0; i < src->height; i++) {
        for (int j = 0; j < src->width; j++) {
            *(dest->data + i * dest->width + j) = rgb_to_luma(image_at(src, i, j));
        }
    }
}

static int avg_3x3(const Image* img, int y, int x) {
    int sum = 0;
    // 上一行
    int up = y - 1 < 0 ? 0 : y - 1;
    // 下一行
    int down = y + 1 > img->height - 1 ? img->height - 1 : y + 1;
    // 左一列
    int left = x - 1 < 0 ? 0 : x - 1;
    // 右一列
    int right = x + 1 >= img->width ? img->width - 1 : x + 1;

    sum += img->data[y * img->width + x];
    sum += img->data[up * img->width + x];
    sum += img->data[down * img->width + x];
    sum += img->data[y * img->width + left];
    sum += img->data[y * img->width + right];
    sum += img->data[up * img->width + left];
    sum += img->data[up * img->width + right];
    sum += img->data[down * img->width + left];
    sum += img->data[down * img->width + right];
    return sum / 9;
}

void blur3x3(const Image* src, Image* dest) {
    for (int i = 0; i < src->height; i++) {
        for (int j = 0; j < src->width; j++) {
            *(dest->data + i * src->width + j) = avg_3x3(src, i, j);
        }
    }
}

static int abs_m(int x) {
    if (x < 0)
        return -x;
    else
        return x;
}

// 找边缘
static int sobel_3(const Image* src, int y, int x) {
    // 上一行
    int up = y - 1 < 0 ? 0 : y - 1;
    // 下一行
    int down = y + 1 > src->height - 1 ? src->height - 1 : y + 1;
    // 左一列
    int left = x - 1 < 0 ? 0 : x - 1;
    // 右一列
    int right = x + 1 >= src->width ? src->width - 1 : x + 1;

    int Gx = (src->data[up * src->width + right] + 2 * src->data[y * src->width + right] + src->data[down * src->width + right]) -
             (src->data[up * src->width + left] + 2 * src->data[y * src->width + left] + src->data[down * src->width + left]);

    int Gy = (src->data[down * src->width + left] + 2 * src->data[down * src->width + x] + src->data[down * src->width + right]) -
             (src->data[up * src->width + left] + 2 * src->data[up * src->width + x] + src->data[up * src->width + right]);
    int g = abs_m(Gx) + abs_m(Gy);
    if (g > MAX_CLAMP) g = MAX_CLAMP;
    return g;
}

void sobel(const Image* src, Image* dest) {
    for (int i = 0; i < src->height; i++) {
        for (int j = 0; j < src->width; j++) {
            *(dest->data + i * src->width + j) = sobel_3(src, i, j);
        }
    }
}