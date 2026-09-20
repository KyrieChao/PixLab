#include "filter.h"
#include "image.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Image* image = image_new(8, 8, 3);
    // printf("%d %d %d \n", image->height, image->width, image->channels);
    // int row[8][3] = {
    //     {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 128, 128}, {255, 255, 0}, {0, 255, 255}};

    // for (int i = 0; i < 8; i++) {
    //     for (int j = 0; j < 8; j++) {
    //         unsigned char* p = image_at(image, i, j);
    //         p[0] = row[j][0];
    //         p[1] = row[j][1];
    //         p[2] = row[j][2];
    //     }
    // }
    // Image* gray = image_new(8, 8, 1);
    // to_gray(image, gray);
    // for (int j = 0; j < 8; j++) {
    //     printf("%d ", gray->data[j]);
    // }

    // image_free(image);
    // image_free(gray);

    Image* img = bmp_load("test.bmp");
    if (img == NULL) {
        printf("error\n");
        return -1;
    }
    printf("width=%d height=%d channels=%d\n", img->width, img->height, img->channels);
    Image* gray = image_new(img->width, img->height, 1);
    to_gray(img, gray);
    image_print(gray);
    image_free(gray);
    image_free(img);
    return 0;
}