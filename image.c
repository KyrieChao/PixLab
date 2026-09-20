
#include "image.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    unsigned char bfType[2] = {0};
    unsigned int bfSize = 0;
    unsigned short bfReserved1 = 0;
    unsigned short bfReserved2 = 0;
    unsigned int bfOffBits = 0;
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

    unsigned int biSize, biCompression, biSizeImage, biXPelsPerMeter, biClrUsed, biClrImportant;
    int32_t biWidth, biHeight;
    unsigned short biPlanes, biCount;
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
    if (biCount != 24 || biCompression != 0 ||
        biWidth <= 0 || biSize != 40 ||
        biHeight == 0 || biHeight == INT32_MIN) {
        fclose(fp);
        return NULL;
    }
    int width = biWidth;
    int height = biHeight > 0 ? biHeight : -biHeight;
    Image* img = image_new(width, height, 3);
    if (!img) {
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return img;
}