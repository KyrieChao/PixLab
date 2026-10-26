#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/* BMP 头里跨位深仍值得保留的字段。
   早先这里存了 8 个字段，唯一用途是让 24 位输入能"字节级原样回写"。
   全格式支持后那个承诺不再成立 —— 32 位输入没法原样写回 24 位输出，
   沿用原始 biSizeImage 反而会写出错误的头，故精简为此。 */
typedef struct {
    int      valid; // 是否来自 bmp_load
    uint32_t xppm;  // 水平物理分辨率（像素/米）
    uint32_t yppm;  // 垂直物理分辨率
    uint16_t reserved1;
    uint16_t reserved2;
} BmpMeta;

/* 内存中的图像：一维连续缓冲，按行优先。
   channels 为 1（灰度）或 3（RGB，顺序 R,G,B）。 */
typedef struct {
    int width, height, channels;
    unsigned char* data;
    BmpMeta bmp; // 非 BMP 来源时为 valid=0
} Image;

/* 分配一张图。尺寸非法或内存不足返回 NULL（不初始化像素内容）。 */
Image* image_new(int width, int height, int channels);

/* 深拷贝一张图，含 bmp meta。失败返回 NULL。 */
Image* image_clone(const Image* src);

/* 在终端用字符画预览。3 通道图会先转成亮度再画。 */
void image_print(const Image* img);

void image_free(Image* img);

/* 取 (x, y) 处像素的首字节指针，调用者按 channels 访问。 */
unsigned char* image_at(const Image* img, int y, int x);

#endif
