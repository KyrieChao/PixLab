#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CLAMP 255

typedef struct {
    int width, height, channels;
    unsigned char* data;
} Image;

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

int main() {
    Image* image = image_new(8, 8, 3);
    printf("%d %d %d \n", image->height, image->width, image->channels);
    int row[8][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}};

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            unsigned char* p = image_at(image, i, j);
            p[0] = row[j][0];
            p[1] = row[j][1];
            p[2] = row[j][2];
        }
    }
    Image* gray = image_new(8, 8, 1);
    to_gray(image, gray);
    for (int j = 0; j < 8; j++) {
        printf("%d ", gray->data[j]);
    }

    image_free(image);
    image_free(gray);
    return 0;
}