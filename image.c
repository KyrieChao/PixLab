
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
    img->bmp.valid = 0;
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
    unsigned int biYPelsPerMeter;     // 垂直分辨率
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
        fread(&biYPelsPerMeter, 4, 1, fp) != 1 ||
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
    // 保留原始头字段，供 bmp_save 原样回写
    img->bmp.valid = 1;
    img->bmp.bfReserved1 = bfReserved1;
    img->bmp.bfReserved2 = bfReserved2;
    img->bmp.biHeight_signed = biHeight; // 保留符号
    img->bmp.biSizeImage = biSizeImage;
    img->bmp.biXPelsPerMeter = biXPelsPerMeter;
    img->bmp.biYPelsPerMeter = biYPelsPerMeter;
    img->bmp.biClrUsed = biClrUsed;
    img->bmp.biClrImportant = biClrImportant;
    // 每行像素数据按 4 字节对齐，stride 是包含填充字节的实际行宽。
    size_t stride = ((size_t)width * 3 + 3) & ~(size_t)3;
    fseek(fp, bfOffBits, SEEK_SET);
    unsigned char* row = malloc(stride);
    if (!row) {
        image_free(img);
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

static void write_u16_le(FILE* fp, uint16_t v) {
    unsigned char b[2];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    fwrite(b, 1, 2, fp);
}

static void write_u32_le(FILE* fp, uint32_t v) {
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
    fwrite(b, 1, 4, fp);
}

static void write_bmp_file_header(FILE* fp, uint32_t file_size, uint32_t off_bits, uint16_t reserved1, uint16_t reserved2) {
    fputc('B', fp);
    fputc('M', fp);
    write_u32_le(fp, file_size); // bfSize
    write_u16_le(fp, reserved1); // bfReserved1
    write_u16_le(fp, reserved2); // bfReserved2
    write_u32_le(fp, off_bits);  // bfOffBits
}

static void write_bmp_info_header(FILE* fp, int32_t width,
                                  int32_t height_signed, // 保留符号
                                  uint16_t bit_count, uint32_t size_image, uint32_t xppm,
                                  uint32_t yppm, uint32_t clr_used, uint32_t clr_important) {
    write_u32_le(fp, 40);
    write_u32_le(fp, (uint32_t)width);
    write_u32_le(fp, (uint32_t)height_signed); // 有符号直接按位写
    write_u16_le(fp, 1);
    write_u16_le(fp, bit_count);
    write_u32_le(fp, 0); // biCompression
    write_u32_le(fp, size_image);
    write_u32_le(fp, xppm);
    write_u32_le(fp, yppm);
    write_u32_le(fp, clr_used);
    write_u32_le(fp, clr_important);
}

int bmp_save(const char* path, Image* img) {
    FILE* fp = fopen(path, "wb");
    if (!fp) return -1;

    int width = img->width;
    int height = img->height;
    int channels = img->channels;

    // 只支持 1 通道（灰度）和 3 通道（RGB），输出都是 24 位 BMP
    if (channels != 1 && channels != 3) {
        fclose(fp);
        return -1;
    }

    // 无论输入是 1 通道还是 3 通道，写出的每像素都是 3 字节 BGR
    size_t row_bytes = (size_t)width * 3;
    size_t row_padded = (row_bytes + 3) & ~(size_t)3;
    uint64_t pixel_bytes = (uint64_t)row_padded * (uint64_t)height;

    if (pixel_bytes > 0xFFFFFFFFu - 54u) { // BMP 总大小不能超 4GB
        fclose(fp);
        return -1;
    }
    uint32_t file_size = (uint32_t)(14 + 40 + pixel_bytes);

    // 有 meta 用 meta，没有则用默认值
    uint16_t bfR1 = 0, bfR2 = 0;
    int32_t h_signed = height;
    uint32_t size_image = (uint32_t)pixel_bytes;
    uint32_t xppm = 0, yppm = 0, clr_used = 0, clr_imp = 0;

    if (img->bmp.valid) {
        bfR1 = img->bmp.bfReserved1;
        bfR2 = img->bmp.bfReserved2;
        h_signed = img->bmp.biHeight_signed;
        size_image = img->bmp.biSizeImage;
        xppm = img->bmp.biXPelsPerMeter;
        yppm = img->bmp.biYPelsPerMeter;
        clr_used = img->bmp.biClrUsed;
        clr_imp = img->bmp.biClrImportant;
    }

    write_bmp_file_header(fp, file_size, 54, bfR1, bfR2);
    write_bmp_info_header(fp, width, h_signed, 24,
                          size_image, xppm, yppm, clr_used, clr_imp);

    int bottom_up = (h_signed > 0);

    for (int i = 0; i < height; i++) {
        int y = bottom_up ? (height - 1 - i) : i;
        for (int x = 0; x < width; x++) {
            unsigned char r, g, b;
            if (channels == 1) {
                // 灰度：R = G = B = luma
                unsigned char l = img->data[y * width + x];
                r = g = b = l;
            } else {
                r = img->data[(y * width + x) * 3 + 0];
                g = img->data[(y * width + x) * 3 + 1];
                b = img->data[(y * width + x) * 3 + 2];
            }
            if (fputc(b, fp) == EOF) {
                fclose(fp);
                return -1;
            }
            if (fputc(g, fp) == EOF) {
                fclose(fp);
                return -1;
            }
            if (fputc(r, fp) == EOF) {
                fclose(fp);
                return -1;
            }
        }
        for (size_t k = row_bytes; k < row_padded; k++) {
            if (fputc(0, fp) == EOF) {
                fclose(fp);
                return -1;
            }
        }
    }

    fclose(fp);
    return 0;
}