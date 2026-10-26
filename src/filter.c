#include "filter.h"
#include "util.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================ 内部辅助 ============================ */

/* 邻域取样时把坐标夹到图像内（复制边缘像素）。
   比"跳过越界"简单，也不会在边框留下一圈黑边。 */
static int clamp_idx(int v, int n) {
    if (v < 0) return 0;
    if (v >= n) return n - 1;
    return v;
}

/* 建一张同尺寸的图并继承 BMP 元信息（分辨率），避免滤镜链后丢失。 */
static Image* new_like(const Image* src, int channels) {
    Image* dst = image_new(src->width, src->height, channels);
    if (dst) dst->bmp = src->bmp;
    return dst;
}

/* 逐字节套同一张 256 项查找表。
   亮度/对比度/伽马都只依赖单通道值，用 LUT 可以完全避开逐像素浮点运算。 */
static Image* map_lut(const Image* src, const unsigned char* lut) {
    Image* dst = image_clone(src);
    if (!dst) return NULL;
    size_t n = (size_t)src->width * src->height * src->channels;
    for (size_t i = 0; i < n; i++) dst->data[i] = lut[src->data[i]];
    return dst;
}

/* ============================ 原有四个滤镜 ============================ */

static Image* f_gray(const Image* src, double arg) {
    (void)arg;
    if (src->channels == 1) return image_clone(src);

    Image* dst = new_like(src, 1);
    if (!dst) return NULL;
    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            const unsigned char* p = image_at(src, y, x);
            dst->data[(size_t)y * src->width + x] = luma_u8(p[0], p[1], p[2]);
        }
    }
    return dst;
}

static Image* f_invert(const Image* src, double arg) {
    (void)arg;
    Image* dst = image_clone(src);
    if (!dst) return NULL;
    size_t n = (size_t)src->width * src->height * src->channels;
    for (size_t i = 0; i < n; i++) dst->data[i] = (unsigned char)(255 - src->data[i]);
    return dst;
}

/* 任意 n×n 均值模糊，用积分图（summed area table）实现：
   每像素 O(1)，与核大小无关 —— 直接卷积是 O(n²)，n=51 时慢 289 倍。

   边界按"有效区域截断"求平均，而不是复制边缘像素。复制边缘在核很大时
   会让边框像素被反复计权，边缘出现偏色。 */
static Image* box_blur_n(const Image* src, int n) {
    int w = src->width, h = src->height, ch = src->channels;

    if (n < 1) n = 1;
    if (n > 999) n = 999;
    if ((n & 1) == 0) n++; /* 偶数核没有中心像素，统一向上取奇数 */
    int r = n / 2;

    Image* dst = image_clone(src);
    if (!dst) return NULL;

    /* SAT 尺寸 (w+1) x (h+1)，多出的一行一列是全零哨兵，
       这样矩形和公式不必对 x0==0 / y0==0 单独分支。
       1748x1082 的图约占 15MB 临时内存。 */
    size_t sw = (size_t)w + 1;
    uint64_t* sat = malloc(sw * ((size_t)h + 1) * sizeof(uint64_t));
    if (!sat) {
        image_free(dst);
        return NULL;
    }

    for (int c = 0; c < ch; c++) {
        for (size_t x = 0; x < sw; x++) sat[x] = 0; /* 哨兵行恒为 0 */

        for (int y = 0; y < h; y++) {
            uint64_t* cur = sat + (size_t)(y + 1) * sw;
            const uint64_t* prev = sat + (size_t)y * sw;
            uint64_t rowsum = 0;
            cur[0] = 0;
            for (int x = 0; x < w; x++) {
                rowsum += src->data[((size_t)y * w + x) * ch + c];
                cur[x + 1] = prev[x + 1] + rowsum;
            }
        }

        for (int y = 0; y < h; y++) {
            int y0 = (y - r < 0) ? 0 : y - r;
            int y1 = (y + r >= h) ? h - 1 : y + r;
            const uint64_t* a = sat + (size_t)y0 * sw;
            const uint64_t* b = sat + (size_t)(y1 + 1) * sw;
            for (int x = 0; x < w; x++) {
                int x0 = (x - r < 0) ? 0 : x - r;
                int x1 = (x + r >= w) ? w - 1 : x + r;
                /* 先各自作差再相减，保证每一步都在无符号域内非负
                   （写成 b[x1+1]-b[x0]-a[x1+1]+a[x0] 中间会下溢） */
                uint64_t sum = (b[x1 + 1] - a[x1 + 1]) - (b[x0] - a[x0]);
                uint64_t cnt = (uint64_t)(y1 - y0 + 1) * (uint64_t)(x1 - x0 + 1);
                dst->data[((size_t)y * w + x) * ch + c] =
                    (unsigned char)((sum + cnt / 2) / cnt); /* 四舍五入 */
            }
        }
    }

    free(sat);
    return dst;
}

static Image* f_blur(const Image* src, double arg) {
    return box_blur_n(src, (int)(arg + 0.5));
}

/* Sobel 边缘检测。彩色输入先转灰度，输出恒为单通道梯度幅值。 */
static Image* f_sobel(const Image* src, double arg) {
    (void)arg;
    Image* gray_tmp = NULL;
    const Image* g = src;
    if (src->channels != 1) {
        gray_tmp = f_gray(src, 0);
        if (!gray_tmp) return NULL;
        g = gray_tmp;
    }

    int w = g->width, h = g->height;
    Image* dst = new_like(src, 1);
    if (!dst) {
        image_free(gray_tmp);
        return NULL;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int up = clamp_idx(y - 1, h), down = clamp_idx(y + 1, h);
            int left = clamp_idx(x - 1, w), right = clamp_idx(x + 1, w);
#define G(yy, xx) g->data[(size_t)(yy) * w + (xx)]
            int gx = (G(up, right) + 2 * G(y, right) + G(down, right)) -
                     (G(up, left) + 2 * G(y, left) + G(down, left));
            int gy = (G(down, left) + 2 * G(down, x) + G(down, right)) -
                     (G(up, left) + 2 * G(up, x) + G(up, right));
#undef G
            int mag = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
            dst->data[(size_t)y * w + x] = clamp_u8(mag);
        }
    }

    image_free(gray_tmp);
    return dst;
}

/* ============================ 基础增强组 ============================ */

static Image* f_brightness(const Image* src, double arg) {
    unsigned char lut[256];
    for (int i = 0; i < 256; i++) lut[i] = clamp_u8((int)(i + arg + 0.5));
    return map_lut(src, lut);
}

static Image* f_contrast(const Image* src, double arg) {
    unsigned char lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = clamp_u8((int)((i - 128) * arg + 128 + 0.5));
    return map_lut(src, lut);
}

static Image* f_gamma(const Image* src, double arg) {
    if (arg <= 0.0) arg = 1.0;
    unsigned char lut[256];
    for (int i = 0; i < 256; i++) {
        double v = pow((double)i / 255.0, 1.0 / arg) * 255.0;
        lut[i] = clamp_u8((int)(v + 0.5));
    }
    return map_lut(src, lut);
}

/* 饱和度：把每个通道朝亮度收缩/发散。f=0 得到灰度，f=1 原样，f>1 更艳。 */
static Image* f_saturate(const Image* src, double arg) {
    if (src->channels == 1) return image_clone(src); // 灰度图无从谈饱和度

    Image* dst = image_clone(src);
    if (!dst) return NULL;

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            const unsigned char* p = image_at(src, y, x);
            unsigned char* q = image_at(dst, y, x);
            int l = luma_u8(p[0], p[1], p[2]);
            for (int c = 0; c < 3; c++)
                q[c] = clamp_u8((int)(l + (p[c] - l) * arg + 0.5));
        }
    }
    return dst;
}

/* 可分离高斯模糊：水平一趟、垂直一趟，复杂度从 O(k²) 降到 O(2k)。
   半径取 3σ，覆盖 99.7% 的权重，再远的截断不可见。 */
static Image* gaussian_blur(const Image* src, double sigma) {
    if (sigma < 0.1) sigma = 0.1;
    if (sigma > 50.0) sigma = 50.0;

    int radius = (int)(3.0 * sigma + 0.5);
    if (radius < 1) radius = 1;

    size_t ksize = (size_t)(2 * radius + 1);
    double* k = malloc(ksize * sizeof(double));
    if (!k) return NULL;

    double s2 = 2.0 * sigma * sigma, sum = 0.0;
    for (int i = -radius; i <= radius; i++) {
        double v = exp(-(double)(i * i) / s2);
        k[i + radius] = v;
        sum += v;
    }
    for (size_t i = 0; i < ksize; i++) k[i] /= sum;

    int w = src->width, h = src->height, ch = src->channels;
    Image* tmp = new_like(src, ch);
    Image* dst = new_like(src, ch);
    if (!tmp || !dst) {
        free(k);
        image_free(tmp);
        image_free(dst);
        return NULL;
    }

    for (int y = 0; y < h; y++) { // 水平
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < ch; c++) {
                double acc = 0.0;
                for (int j = -radius; j <= radius; j++) {
                    int xx = clamp_idx(x + j, w);
                    acc += k[j + radius] * src->data[((size_t)y * w + xx) * ch + c];
                }
                tmp->data[((size_t)y * w + x) * ch + c] = clamp_u8((int)(acc + 0.5));
            }
        }
    }
    for (int y = 0; y < h; y++) { // 垂直
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < ch; c++) {
                double acc = 0.0;
                for (int j = -radius; j <= radius; j++) {
                    int yy = clamp_idx(y + j, h);
                    acc += k[j + radius] * tmp->data[((size_t)yy * w + x) * ch + c];
                }
                dst->data[((size_t)y * w + x) * ch + c] = clamp_u8((int)(acc + 0.5));
            }
        }
    }

    free(k);
    image_free(tmp);
    return dst;
}

static Image* f_gaussian(const Image* src, double arg) {
    return gaussian_blur(src, arg);
}

/* USM 锐化：原图 + amount × (原图 − 高斯模糊)。差值即高频细节。 */
static Image* f_sharpen(const Image* src, double arg) {
    if (arg < 0.0) arg = 0.0;

    Image* bl = gaussian_blur(src, 1.0);
    if (!bl) return NULL;
    Image* dst = image_clone(src);
    if (!dst) {
        image_free(bl);
        return NULL;
    }

    size_t n = (size_t)src->width * src->height * src->channels;
    for (size_t i = 0; i < n; i++) {
        int v = (int)src->data[i];
        int detail = v - (int)bl->data[i];
        dst->data[i] = clamp_u8((int)(v + arg * detail + 0.5));
    }
    image_free(bl);
    return dst;
}

/* k×k 中值滤波：对椒盐噪声特别有效，且不像均值模糊那样糊掉边缘。 */
static Image* f_median(const Image* src, double arg) {
    int k = (int)(arg + 0.5);
    if (k < 3) k = 3;
    if (k > 9) k = 9;
    if ((k & 1) == 0) k++; // 中值必须取奇数窗口的中心
    int half = k / 2;

    int w = src->width, h = src->height, ch = src->channels;
    Image* dst = image_clone(src);
    if (!dst) return NULL;

    int vals[81]; // 9*9 上界
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < ch; c++) {
                int n = 0;
                for (int dy = -half; dy <= half; dy++) {
                    int yy = clamp_idx(y + dy, h);
                    for (int dx = -half; dx <= half; dx++) {
                        int xx = clamp_idx(x + dx, w);
                        vals[n++] = src->data[((size_t)yy * w + xx) * ch + c];
                    }
                }
                for (int i = 1; i < n; i++) { // 插入排序，n ≤ 81
                    int key = vals[i], j = i - 1;
                    while (j >= 0 && vals[j] > key) {
                        vals[j + 1] = vals[j];
                        j--;
                    }
                    vals[j + 1] = key;
                }
                dst->data[((size_t)y * w + x) * ch + c] = (unsigned char)vals[n / 2];
            }
        }
    }
    return dst;
}

/* 大津法（Otsu）：在 0..255 里找那个"把直方图分成两堆、两堆差得最开"的阈值。
   对白底黑字这类双峰直方图的图，它自动落在两峰之间的谷底，不用手调。

   判据是类间方差 wB*wF*(mB-mF)^2：wB/wF 是两堆的像素数、mB/mF 是两堆的均值。
   乘上像素数是为了不让"只有一个像素的堆"因为均值差得远而胜出。 */
static int otsu_threshold(const Image* src) {
    unsigned long long hist[256];
    for (int i = 0; i < 256; i++) hist[i] = 0;

    int w = src->width, h = src->height, ch = src->channels;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char* p = image_at(src, y, x);
            unsigned char v = (ch == 1) ? p[0] : luma_u8(p[0], p[1], p[2]);
            hist[v]++;
        }
    }

    unsigned long long total = (unsigned long long)w * (unsigned long long)h;
    unsigned long long sum_all = 0;
    for (int i = 0; i < 256; i++) sum_all += (unsigned long long)i * hist[i];

    unsigned long long wB = 0, sumB = 0;
    double max_var = -1.0;
    int best = 128;

    for (int t = 0; t < 256; t++) {
        wB += hist[t];
        if (wB == 0) continue;
        unsigned long long wF = total - wB;
        if (wF == 0) break; /* 剩下的全在暗堆里，再往下切没意义 */
        sumB += (unsigned long long)t * hist[t];

        double mB = (double)sumB / (double)wB;
        double mF = (double)(sum_all - sumB) / (double)wF;
        double d = mB - mF;
        double var = (double)wB * (double)wF * d * d;
        if (var > max_var) {
            max_var = var;
            best = t;
        }
    }
    return best;
}

/* 二值化：亮度 > t 变纯白，否则变纯黑，中间灰阶全部消失。
   与 contrast 的区别是"坍缩"而非"拉伸" —— 浅灰的网格背景会彻底消失，
   而不是仅仅变淡。这正是处理电路图 / 文档 / 扫描件最需要的效果。

   输出恒为单通道：结果只有 0 和 255 两个值，占 3 通道没有意义。 */
static Image* f_threshold(const Image* src, double arg) {
    int w = src->width, h = src->height, ch = src->channels;

    /* 约定 arg <= 0 表示"没给参数"，自动用 Otsu 算阈值。
       自动模式把算出的值报到 stderr（不进 stdout），否则用户拿到的是黑盒，
       看不出 Otsu 到底选了哪条线 —— 调参时这个信息很关键。 */
    int t;
    if (arg <= 0.0) {
        t = otsu_threshold(src);
        fprintf(stderr, "  Otsu 自动阈值：%d\n", t);
    } else {
        t = (int)(arg + 0.5);
    }
    if (t < 0) t = 0;
    if (t > 255) t = 255;

    Image* dst = new_like(src, 1);
    if (!dst) return NULL;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char* p = image_at(src, y, x);
            unsigned char v = (ch == 1) ? p[0] : luma_u8(p[0], p[1], p[2]);
            dst->data[(size_t)y * w + x] = (v > t) ? 255 : 0;
        }
    }
    return dst;
}

/* ============================ 滤镜表 ============================ */

static const Filter g_filters[] = {
    {"gray",       "转灰度（RGB → 亮度）",                 0, 0.0, f_gray},
    {"invert",     "负片（逐通道取反）",                   0, 0.0, f_invert},
    {"blur",       "均值模糊：n×n 核，n 越大越糊（例 blur:9）", 1, 3.0, f_blur},
    {"sobel",      "Sobel 边缘检测（先转灰度）",           0, 0.0, f_sobel},
    {"brightness", "亮度：每通道加 n（例 brightness:20）", 1, 0.0, f_brightness},
    {"contrast",   "对比度：(v-128)*f+128，f=1 不变",      1, 1.0, f_contrast},
    {"saturate",   "饱和度：f=0 全灰，f=1 不变，>1 更艳",  1, 1.0, f_saturate},
    {"gamma",      "伽马校正 out=in^(1/g)：g>1 提亮、g<1 压暗", 1, 1.0, f_gamma},
    {"gaussian",   "高斯模糊：sigma 越大越糊",             1, 1.0, f_gaussian},
    {"sharpen",    "USM 锐化：amount 越大越锐",            1, 1.0, f_sharpen},
    {"median",     "k×k 中值去噪（奇数 k，3..9）",         1, 3.0, f_median},
    {"threshold",  "二值化：亮度>t 变白、否则变黑；t=0 自动 Otsu", 1, 0.0, f_threshold},
    {NULL, NULL, 0, 0.0, NULL}
};

const Filter* filter_table(void) {
    return g_filters;
}

const Filter* filter_find(const char* name) {
    if (!name) return NULL;
    for (const Filter* f = g_filters; f->name; f++) {
        if (strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}
