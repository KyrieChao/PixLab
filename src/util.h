#ifndef PIXLAB_UTIL_H
#define PIXLAB_UTIL_H

#include <stddef.h>

/* 上限：超出的文件直接拒绝，避免恶意头字段诱发天量分配。
   1 亿像素约合 12000x8000，已远超普通照片。 */
#define PIXLAB_MAX_PIXELS (100u * 1000u * 1000u)
#define PIXLAB_MAX_FILE   (256u * 1024u * 1024u)

/* 把整数钳制到 [0, 255]。
   所有像素运算的结果都经它收敛，避免滤镜链里层层溢出。 */
static inline unsigned char clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (unsigned char)v;
}

/* ITU-R BT.601 亮度的整数近似（权重和恰为 256，故右移 8 位）。
   灰度化、饱和度、字符画预览共用同一条公式，保证各处一致。 */
static inline unsigned char luma_u8(unsigned char r, unsigned char g, unsigned char b) {
    return (unsigned char)((77 * r + 150 * g + 29 * b) >> 8);
}

/* 安全乘法：a*b 溢出 size_t 时返回 0；否则写入 *out 并返回 1。
   BMP 里的宽高来自文件，必须用这个而不是裸乘。 */
int mul_ok(size_t a, size_t b, size_t* out);

/* 读取整个文件到堆缓冲。
   成功返回 1 并设置 *out / *len，调用者负责 free(*out)。
   打开失败、文件为空、或超过 max_bytes 时返回 0。 */
int read_entire_file(const char* path, size_t max_bytes,
                     unsigned char** out, size_t* len);

#endif
