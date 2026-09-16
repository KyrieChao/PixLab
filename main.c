#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CLAMP 255

typedef struct {
    size_t width, height, channels;
    unsigned char* data;
} Image;

Image* image_new(size_t width, size_t height, size_t channels) {
    Image* img = malloc(sizeof(Image));
    if (!img) return NULL;
    img->width = width;
    img->height = height;
    img->channels = channels;
    size_t size = width * height * channels;
    img->data = malloc(size);
    if (!img->data) {
        free(img);
        return NULL;
    }
    return img;
}

void print_img(const unsigned char img[][8], int rows) {
    static const char* lv[] = {"  ", "..", "::", "--", "==", "**", "##", "@@"};
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < 8; j++) {
            printf("%s", lv[img[i][j] / 32]);
        }
        printf("\n");
    }
}

void image_print(const Image* img) {
    if (img->channels == 1) {
        static const char* lv[] = {"  ", "..", "::", "--", "==", "**", "##", "@@"};
        for (size_t i = 0; i < img->height; i++) {
            for (size_t j = 0; j < img->width; j++) {
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

unsigned char* image_at(Image* img, size_t y, size_t x) {
    return img->data + (x + y * img->width) * img->channels;
}

void invert(unsigned char img[][8], int rows) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < 8; j++) {
            img[i][j] = 255 - img[i][j];
        }
    }
}

unsigned char rgb_to_luma(const unsigned char* rgb) {
    return (77 * rgb[0] + 150 * rgb[1] + 29 * rgb[2]) >> 8;
}

void to_gray(const unsigned char color[][8][3], unsigned char gray[][8], int rows) {

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < 8; j++) {
            gray[i][j] = rgb_to_luma(color[i][j]);
        }
    }
}

static int avg_3x3(const unsigned char gray[][8], int y, int x, int rows) {
    int sum = 0;
    // 上一行
    int up = y - 1 < 0 ? 0 : y - 1;
    // 下一行
    int down = y + 1 > rows - 1 ? rows - 1 : y + 1;
    // 左一列
    int left = x - 1 < 0 ? 0 : x - 1;
    // 右一列
    int right = x + 1 > 7 ? 7 : x + 1;
    sum += gray[y][x];
    sum += gray[up][x];
    sum += gray[down][x];
    sum += gray[y][left];
    sum += gray[y][right];
    sum += gray[up][left];
    sum += gray[up][right];
    sum += gray[down][left];
    sum += gray[down][right];
    return sum / 9;
}

void blur3x3(const unsigned char gray[][8], unsigned char out[][8], int rows) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < 8; j++) {
            out[i][j] = avg_3x3(gray, i, j, rows);
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
static int sobel_3(const unsigned char gray[][8], int y, int x, int rows) {
    // 上一行
    int up = y - 1 < 0 ? 0 : y - 1;
    // 下一行
    int down = y + 1 > rows - 1 ? rows - 1 : y + 1;
    // 左一列
    int left = x - 1 < 0 ? 0 : x - 1;
    // 右一列
    int right = x + 1 > 7 ? 7 : x + 1;
    int Gx = (gray[up][right] + 2 * gray[y][right] + gray[down][right]) -
             (gray[up][left] + 2 * gray[y][left] + gray[down][left]);

    int Gy = (gray[down][left] + 2 * gray[down][x] + gray[down][right]) -
             (gray[up][left] + 2 * gray[up][x] + gray[up][right]);
    int g = abs_m(Gx) + abs_m(Gy);
    // printf("Gx=%04d Gy=%04d\n", Gx, Gy);
    if (g > MAX_CLAMP) g = MAX_CLAMP;
    return g;
}

void sobel(const unsigned char gray[][8], unsigned char out[][8], int rows) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < 8; j++) {
            out[i][j] = sobel_3(gray, i, j, rows);
        }
    }
}

int main() {
    // unsigned char img[8][8] = {
    //     {0, 0, 255, 0, 0, 255, 0, 0},
    //     {0, 0, 255, 0, 0, 255, 0, 0},
    //     {0, 0, 255, 0, 0, 255, 0, 0},
    //     {0, 0, 255, 255, 255, 255, 0, 0},
    //     {0, 0, 255, 0, 0, 255, 0, 0},
    //     {0, 0, 255, 0, 0, 255, 0, 0},
    //     {0, 0, 255, 0, 0, 255, 0, 0},
    //     {0, 0, 255, 0, 0, 255, 0, 0},
    // };
    // print_img(img, 8);
    // invert(img, 8);
    // printf("\n");
    // print_img(img, 8);

    // unsigned char color[8][8][3] = {
    //     {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}},
    //     {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}},
    //     {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}},
    //     {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}},
    //     {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}},
    //     {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}},
    //     {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}},
    //     {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}},
    // };

    // unsigned char rgb[8][8];
    // unsigned char rgb_blur[8][8];
    // memset(color[2], 0, sizeof(color[2]));
    // to_gray(color, rgb, 8);
    // print_img(rgb, 8);
    // blur3x3(rgb, rgb_blur, 8);
    // print_img(rgb_blur, 8);

    // unsigned char edge_src[8][8];
    // unsigned char edge_mag[8][8];
    // for (int i = 0; i < 8; i++) {
    //     for (int j = 0; j < 8; j++) {
    //         if (j <= 3)
    //             edge_src[i][j] = 0;
    //         else
    //             edge_src[i][j] = 255;
    //     }
    // }
    // sobel(edge_src, edge_mag, 8);
    // print_img(edge_mag, 8);
    Image* image = image_new(8, 8, 1);
    printf("%zu %zu %zu \n", image->height, image->width, image->channels);
    int arr[] = {76, 149, 28, 255, 0, 128, 226, 178};
    for (size_t i = 0; i < 8; i++) {
        for (size_t j = 0; j < 8; j++) {
            if (i == 1)
                image->data[i * image->width + j] = 0;
            else
                image->data[i * image->width + j] = arr[j];
        }
    }
    image_print(image);
    image_free(image);
    return 0;
}