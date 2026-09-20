#ifndef IMAGE_H
#define IMAGE_H

#define MAX_CLAMP 255

typedef struct image {
    int width, height, channels;
    unsigned char* data;
} Image;

Image* image_new(int width, int height, int channels);

void image_print(const Image* img);

void image_free(Image* img);

unsigned char* image_at(const Image* img, int y, int x);

Image* bmp_load(const char* path);

#endif