#ifndef FILTER_H
#define FILTER_H
#include "image.h"
#include <stddef.h>

// 只接受单通道图
void invert(Image* img);

// 泛化
void invert_map(Image* img);

unsigned char rgb_to_luma(const unsigned char* rgb);

void to_gray(const Image* src, Image* dest);

void blur3x3(const Image* src, Image* dest);

void sobel(const Image* src, Image* dest);

#endif