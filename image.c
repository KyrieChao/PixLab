
#include "image.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Image* image_new(int width, int height, int channels) {
    Image* img = malloc(sizeof(Image));
    if (!img) return NULL;
    img->width = width;
    img->height = height;
    img->channels = channels;
    size_t size = (size_t)width * (size_t)height * (size_t)channels;
    img->data = malloc(size);
    if (!img->data) {
        free(img);
        return NULL;
    }
    return img;
}

void image_print(const Image* img) {
    if (img->channels == 1) {
        static const char* lv[] = {"  ", "..", "::", "--", "==", "**", "##", "@@"};
        for (int i = 0; i < img->height; i++) {
            for (int j = 0; j < img->width; j++) {
                printf("%s", lv[img->data[i * img->width + j] / 32]);
            }
            printf("\n");
        }
    } else
        printf("error\n");
}

void image_free(Image* img) {
    if (!img) return;
    free(img->data);
    free(img);
}

unsigned char* image_at(const Image* img, int y, int x) {
    return img->data + (x + y * img->width) * img->channels;
}

Image* bmp_load(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;

    // BMP 文件头字段：文件类型、文件大小、保留字段和像素数据偏移。
    unsigned char bfType[2] = {0};  // 文件类型，必须为 "BM"
    unsigned int bfSize = 0;        // BMP 文件总大小
    unsigned short bfReserved1 = 0; // 保留字段 1
    unsigned short bfReserved2 = 0; // 保留字段 2
    unsigned int bfOffBits = 0;     // 像素数据相对于文件开头的偏移
    if (fread(bfType, 2, 1, fp) != 1 || bfType[0] != 0x42 || bfType[1] != 0x4D) {
        fclose(fp);
        return NULL;
    }
    if (fread(&bfSize, 4, 1, fp) != 1 ||
        fread(&bfReserved1, 2, 1, fp) != 1 ||
        fread(&bfReserved2, 2, 1, fp) != 1 ||
        fread(&bfOffBits, 4, 1, fp) != 1) {
        fclose(fp);
        return NULL;
    }

    // BITMAPINFOHEADER 字段：描述信息头大小、尺寸、颜色和压缩方式。
    unsigned int biSize;              // 信息头大小，通常为 40 字节
    unsigned int biCompression;       // 压缩方式，0 表示不压缩
    unsigned int biSizeImage;         // 像素数据大小
    unsigned int biXPelsPerMeter;     // 水平分辨率
    unsigned int biClrUsed;           // 使用的颜色数
    unsigned int biClrImportant;      // 重要颜色数
    int32_t biWidth, biHeight;        // 图像宽度和高度
    unsigned short biPlanes, biCount; // 颜色平面数和每像素位数
    if (fread(&biSize, 4, 1, fp) != 1 ||
        fread(&biWidth, 4, 1, fp) != 1 ||
        fread(&biHeight, 4, 1, fp) != 1 ||
        fread(&biPlanes, 2, 1, fp) != 1 ||
        fread(&biCount, 2, 1, fp) != 1 ||
        fread(&biCompression, 4, 1, fp) != 1 ||
        fread(&biSizeImage, 4, 1, fp) != 1 ||
        fread(&biXPelsPerMeter, 4, 1, fp) != 1 ||
        fread(&biClrUsed, 4, 1, fp) != 1 ||
        fread(&biClrImportant, 4, 1, fp) != 1) {
        fclose(fp);
        return NULL;
    }
    // 当前实现只支持 24 位、未压缩的 BMP，且不接受无效尺寸。
    if (biCount != 24 || biCompression != 0 ||
        biWidth <= 0 || biSize != 40 ||
        biHeight == 0 || biHeight == INT32_MIN) {
        fclose(fp);
        return NULL;
    }
    int width = biWidth;
    // 高度为负表示图像按自上而下存储；取绝对值作为内存图像高度。
    int height = biHeight > 0 ? biHeight : -biHeight;
    Image* img = image_new(width, height, 3);
    if (!img) {
        fclose(fp);
        return NULL;
    }
    // 每行像素数据按 4 字节对齐，stride 是包含填充字节的实际行宽。
    size_t stride = ((width * 3 + 3) / 4) * 4;
    fseek(fp, bfOffBits, SEEK_SET);
    unsigned char* row = malloc(stride);
    if (!row) {
        free(img->data);
        free(img);
        fclose(fp);
        return NULL;
    }
    for (int i = 0; i < height; i++) {
        if (fread(row, 1, stride, fp) != stride) {
            free(row);
            free(img->data);
            free(img);
            fclose(fp);
            return NULL;
        }
        int dst_y = 0;
        if (biHeight > 0) {
            dst_y = height - 1 - i;
        } else {
            dst_y = i;
        }
        // memcpy(img->data + dst_y * width * 3, row, width * 3);
        for (int x = 0; x < width; x++) {
            unsigned char b = row[x * 3 + 0];
            unsigned char g = row[x * 3 + 1];
            unsigned char r = row[x * 3 + 2];
            unsigned char* dst = image_at(img, dst_y, x);
            dst[0] = r; // 红
            dst[1] = g; // 绿
            dst[2] = b; // 蓝
        }
    }
    free(row);
    fclose(fp);
    return img;
}
