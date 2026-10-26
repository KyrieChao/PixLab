#ifndef PIXLAB_BMP_H
#define PIXLAB_BMP_H

#include "image.h"

#include <stddef.h>
#include <stdint.h>

/* 解码失败的原因。
   早先 bmp_load 无论什么毛病都只返回 NULL，用户完全不知道是文件坏了、
   位深不支持、还是压缩方式没实现。现在每一步都给出具体原因。 */
typedef enum {
    BMP_OK = 0,
    BMP_ERR_OPEN,                // 打不开（不存在 / 无权限 / 空文件 / 超上限）
    BMP_ERR_TOO_SHORT,           // 文件被截断，头或像素数据读不全
    BMP_ERR_NOT_BMP,             // 前两字节不是 "BM"
    BMP_ERR_BAD_DIB,             // biSize 非法或无法识别
    BMP_ERR_BAD_GEOMETRY,        // 宽高非法、biPlanes != 1
    BMP_ERR_UNSUPPORTED_BPP,     // 位深不在 {1,4,8,16,24,32}
    BMP_ERR_COMPRESSION_JPEG,    // biCompression == BI_JPEG(4)
    BMP_ERR_COMPRESSION_PNG,     // biCompression == BI_PNG(5)
    BMP_ERR_COMPRESSION_UNKNOWN, // 其它未知压缩码
    BMP_ERR_BAD_PALETTE,         // 调色板区域不可用
    BMP_ERR_BAD_MASK,            // 位掩码为零 / 重叠 / 不连续
    BMP_ERR_BAD_OFFSET,          // bfOffBits 落在非法区间
    BMP_ERR_RLE_CORRUPT,         // RLE 码流越界或非法
    BMP_ERR_TOO_MANY_PIXELS,     // 超出 PIXLAB_MAX_PIXELS
    BMP_ERR_OOM                  // 内存不足
} BmpError;

/* 头部解析结果，供 bmp_probe 与 `pixlab info` 使用。 */
typedef struct {
    size_t   file_size;
    uint32_t off_bits;     // 像素数据偏移
    uint32_t header_size;  // biSize 原值
    int32_t  width, height; // 原值，height 为负表示自顶向下
    int      top_down;
    uint16_t planes, bpp;
    uint32_t compression;
    uint32_t size_image;   // biSizeImage 原值（可能为 0）
    int32_t  xppm, yppm;
    uint32_t clr_used, clr_important;
    int      palette_entries; // 实际读入的调色板表项数
    int      has_masks;
    uint32_t mask_r, mask_g, mask_b, mask_a;
    size_t   stride;          // 含填充字节的行宽
} BmpInfo;

/* 读取 BMP 并解码为内存图像。
   成功返回 Image*（调用者 image_free）；失败返回 NULL 并写入 *err。
   解码后一律展开为 3 通道 RGB；调色板为纯灰阶时输出 1 通道灰度图。 */
Image* bmp_load_ex(const char* path, BmpError* err);

/* 兼容旧调用点，失败时不提供原因。 */
Image* bmp_load(const char* path);

/* 写出 24 位无压缩 BMP（恒定 40 字节头、自底向上）。
   1 通道图按 R=G=B 写出。成功返回 0，失败返回 -1。 */
int bmp_save(const char* path, const Image* img);

/* 只解析头部，不解码像素。成功返回 BMP_OK。 */
BmpError bmp_probe(const char* path, BmpInfo* out);

/* 错误码的中文说明。 */
const char* bmp_strerror(BmpError e);

/* 头部类型 / 压缩方式的可读名称，用于 info 输出。 */
const char* bmp_header_name(uint32_t header_size);
const char* bmp_compression_name(uint32_t compression);

#endif
