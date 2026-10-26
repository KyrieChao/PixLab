#include "bmp.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================ 有界游标 ============================
   RLE 解码和偏移校验在"整个文件都在内存里、每次读都查边界"的前提下
   最简单也最不可能越界。代价是峰值内存翻倍，普通照片完全可接受。 */

typedef struct {
    const unsigned char* buf;
    size_t size;
    size_t pos;
} BmpReader;

static int br_read(BmpReader* r, void* dst, size_t n) {
    if (n > r->size - r->pos) return 0; // pos 恒 <= size，不会下溢
    if (dst) memcpy(dst, r->buf + r->pos, n);
    r->pos += n;
    return 1;
}

static int br_skip(BmpReader* r, size_t n) {
    if (n > r->size - r->pos) return 0;
    r->pos += n;
    return 1;
}

static int br_u8(BmpReader* r, unsigned char* v) {
    if (r->pos >= r->size) return 0;
    *v = r->buf[r->pos++];
    return 1;
}

static int br_u16(BmpReader* r, uint16_t* v) {
    unsigned char b[2];
    if (!br_read(r, b, 2)) return 0;
    *v = (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
    return 1;
}

static int br_u32(BmpReader* r, uint32_t* v) {
    unsigned char b[4];
    if (!br_read(r, b, 4)) return 0;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static int br_i32(BmpReader* r, int32_t* v) {
    uint32_t u;
    if (!br_u32(r, &u)) return 0;
    *v = (int32_t)u;
    return 1;
}

static int br_seek(BmpReader* r, size_t pos) {
    if (pos > r->size) return 0;
    r->pos = pos;
    return 1;
}

/* ============================ 位掩码 ============================
   16/32 位图的通道位置由掩码描述，绝不能写死 555 —— 565 更常见，
   V4/V5 头里又是 0x00FF0000 这种形式。这里统一拆成 shift + bits。 */

typedef struct {
    uint32_t mask;
    int shift;
    int bits;
} BmpMask;

typedef struct {
    BmpMask r, g, b, a;
    int active;    // 为 0 表示按固定字节序取（24/32bpp BI_RGB）
    int has_alpha;
} BmpMasks;

/* 把掩码拆成 (尾零个数, 连续 1 的个数)。掩码不是连续一段 1 则失败。 */
static int build_mask(uint32_t mask, BmpMask* out) {
    out->mask = mask;
    out->shift = 0;
    out->bits = 0;
    if (mask == 0) return 1;

    int shift = 0;
    while (((mask >> shift) & 1u) == 0) shift++;
    uint32_t v = mask >> shift;
    int bits = 0;
    while (v & 1u) {
        bits++;
        v >>= 1;
    }
    if (v != 0) return 0; // 中间有洞，无法当成一个通道
    out->shift = shift;
    out->bits = bits;
    return 1;
}

/* 把 bits 位的原始值拉到 0..255。
   用位复制而不是 v*255/max：满量程能精确命中 255，且无舍入偏差、更快。 */
static unsigned char scale_bits(uint32_t v, const BmpMask* m) {
    if (m->bits <= 0) return 0;
    if (m->bits >= 8) return (unsigned char)(v >> (m->bits - 8));
    if (m->bits == 5) return (unsigned char)((v << 3) | (v >> 2));
    if (m->bits == 6) return (unsigned char)((v << 2) | (v >> 4));
    uint32_t max = (1u << m->bits) - 1u;
    return (unsigned char)((v * 255u + max / 2u) / max);
}

static unsigned char mask_value(uint32_t pixel, const BmpMask* m) {
    return scale_bits((pixel & m->mask) >> m->shift, m);
}

/* ============================ 调色板 ============================
   CORE 头表项 3 字节（BGR），INFO 头 4 字节（BGRX）。
   混用是 8bpp 图变色的头号原因，所以这里一律按来源区分 entry_size。 */

#define PAL_MAX 256

typedef struct {
    unsigned char rgb[PAL_MAX][3];
    int count;
    int is_gray; // 全部有效表项 r==g==b，据此决定输出 1 通道还是 3 通道
} BmpPalette;

/* ============================ DIB 头 ============================ */

typedef struct {
    uint32_t header_size;
    int32_t width, height;
    uint16_t planes, bpp;
    uint32_t compression;
    uint32_t size_image;
    int32_t xppm, yppm;
    uint32_t clr_used, clr_important;
    int is_core;        // biSize == 12（OS/2 v1，宽高为无符号 16 位）
    int embedded_masks; // biSize >= 52，掩码内嵌在头里
    uint32_t mask_r, mask_g, mask_b, mask_a;
} BmpDib;

/* 解析 DIB 头。biSize 认识 12/40/52/56/64/108/124；
   落在 41..51、57..63、65..107 这类"野生但合理"的值时按 INFO 布局读前
   40 字节，其余靠 bfOffBits 定位 —— 比直接拒绝 tolerant 得多。 */
static BmpError parse_dib(BmpReader* r, BmpDib* d) {
    memset(d, 0, sizeof(*d));

    uint32_t hdr_size;
    if (!br_u32(r, &hdr_size)) return BMP_ERR_TOO_SHORT;
    d->header_size = hdr_size;

    if (hdr_size == 12) { // BITMAPCOREHEADER
        uint16_t w, h, planes, bpp;
        if (!br_u16(r, &w) || !br_u16(r, &h) ||
            !br_u16(r, &planes) || !br_u16(r, &bpp))
            return BMP_ERR_TOO_SHORT;
        d->is_core = 1;
        d->width = (int32_t)w;
        d->height = (int32_t)h; // CORE 头没有"自顶向下"，恒为正
        d->planes = planes;
        d->bpp = bpp;
        d->compression = 0;
        return BMP_OK;
    }

    if (hdr_size < 40) return BMP_ERR_BAD_DIB;

    if (!br_i32(r, &d->width) || !br_i32(r, &d->height) ||
        !br_u16(r, &d->planes) || !br_u16(r, &d->bpp) ||
        !br_u32(r, &d->compression) || !br_u32(r, &d->size_image) ||
        !br_i32(r, &d->xppm) || !br_i32(r, &d->yppm) ||
        !br_u32(r, &d->clr_used) || !br_u32(r, &d->clr_important))
        return BMP_ERR_TOO_SHORT;

    if (hdr_size >= 52) { // V2/V3/V4/V5：掩码内嵌
        if (!br_u32(r, &d->mask_r) || !br_u32(r, &d->mask_g) ||
            !br_u32(r, &d->mask_b))
            return BMP_ERR_TOO_SHORT;
        d->embedded_masks = 1;
        if (hdr_size >= 56) {
            if (!br_u32(r, &d->mask_a)) return BMP_ERR_TOO_SHORT;
        }
    }

    // 跳到头声明的末尾（V4 还有色彩空间/端点/gamma，V5 还有 ICC profile）
    size_t header_end = 14u + (size_t)hdr_size;
    if (!br_seek(r, header_end)) return BMP_ERR_TOO_SHORT;
    return BMP_OK;
}

/* 决定用哪些掩码。游标停在掩码位置（parse_dib 之后）。 */
static BmpError load_masks(BmpReader* r, const BmpDib* d, BmpMasks* m) {
    memset(m, 0, sizeof(*m));

    uint32_t cr = 0, cg = 0, cb = 0, ca = 0;
    int have = 0;

    if (d->embedded_masks && (d->mask_r || d->mask_g || d->mask_b)) {
        cr = d->mask_r;
        cg = d->mask_g;
        cb = d->mask_b;
        ca = d->mask_a;
        have = 1;
    } else if (d->compression == 3 || d->compression == 6) {
        // 40 字节头 + BI_BITFIELDS：掩码紧跟头之后（偏移 54/58/62[/66]）
        if (!br_u32(r, &cr) || !br_u32(r, &cg) || !br_u32(r, &cb))
            return BMP_ERR_TOO_SHORT;
        if (d->compression == 6 && !br_u32(r, &ca)) return BMP_ERR_TOO_SHORT;
        have = 1;
    } else if (d->bpp == 16) {
        // 16bpp 不带掩码时，规范默认是 5-5-5
        cr = 0x7C00;
        cg = 0x03E0;
        cb = 0x001F;
        have = 1;
    }

    if (!have) return BMP_OK; // 24/32bpp BI_RGB：直接按字节序取

    if (cr == 0 || cg == 0 || cb == 0) return BMP_ERR_BAD_MASK;
    if ((cr & cg) || (cr & cb) || (cg & cb)) return BMP_ERR_BAD_MASK; // 重叠
    if (!build_mask(cr, &m->r) || !build_mask(cg, &m->g) ||
        !build_mask(cb, &m->b))
        return BMP_ERR_BAD_MASK;

    uint32_t full = (d->bpp >= 32) ? 0xFFFFFFFFu : ((1u << d->bpp) - 1u);
    if ((cr & ~full) || (cg & ~full) || (cb & ~full)) return BMP_ERR_BAD_MASK;

    m->active = 1;
    if (ca != 0) {
        if (!build_mask(ca, &m->a)) return BMP_ERR_BAD_MASK;
        m->has_alpha = 1; // 仅记录：本项目无 alpha 概念，一律按不透明处理
    }
    return BMP_OK;
}

/* 读调色板。游标停在调色板起点。容量受 bfOffBits 限制，绝不越界读。 */
static BmpError load_palette(BmpReader* r, const BmpDib* d, uint32_t off_bits,
                             BmpPalette* pal) {
    memset(pal, 0, sizeof(*pal));
    if (d->bpp > 8) return BMP_OK; // 16/24/32bpp 没有调色板

    int entry_size = d->is_core ? 3 : 4;
    size_t start = r->pos;
    if (off_bits < start) return BMP_ERR_BAD_OFFSET;

    size_t capacity = (size_t)(off_bits - start) / (size_t)entry_size;
    size_t want = d->clr_used ? (size_t)d->clr_used : ((size_t)1 << d->bpp);
    if (want > capacity) want = capacity; // 声明数超过实际可用空间就截断
    if (want > PAL_MAX) want = PAL_MAX;
    if (want == 0) return BMP_ERR_BAD_PALETTE;

    if (!br_seek(r, start)) return BMP_ERR_TOO_SHORT;

    int is_gray = 1;
    for (size_t i = 0; i < want; i++) {
        unsigned char e[4] = {0, 0, 0, 0};
        if (!br_read(r, e, (size_t)entry_size)) return BMP_ERR_TOO_SHORT;
        pal->rgb[i][0] = e[2]; // R
        pal->rgb[i][1] = e[1]; // G
        pal->rgb[i][2] = e[0]; // B
        if (e[0] != e[1] || e[1] != e[2]) is_gray = 0;
    }
    pal->count = (int)want;
    pal->is_gray = is_gray;
    return BMP_OK;
}

/* ============================ 几何校验 ============================
   所有尺寸都用 64 位中间量，且"先确认像素数据真的够读"再分配。 */

static BmpError validate(const BmpDib* d, uint32_t off_bits, size_t file_size,
                         int* aw, int* ah, int* top_down, size_t* stride) {
    switch (d->bpp) {
    case 1: case 4: case 8: case 16: case 24: case 32: break;
    default: return BMP_ERR_UNSUPPORTED_BPP;
    }

    switch (d->compression) {
    case 0: break; // BI_RGB
    case 1:
        if (d->bpp != 8) return BMP_ERR_BAD_GEOMETRY;
        break; // BI_RLE8
    case 2:
        if (d->bpp != 4) return BMP_ERR_BAD_GEOMETRY;
        break; // BI_RLE4
    case 3:
    case 6:
        if (d->bpp != 16 && d->bpp != 32) return BMP_ERR_BAD_GEOMETRY;
        break;
    case 4: return BMP_ERR_COMPRESSION_JPEG;
    case 5: return BMP_ERR_COMPRESSION_PNG;
    default: return BMP_ERR_COMPRESSION_UNKNOWN;
    }

    if (d->width <= 0) return BMP_ERR_BAD_GEOMETRY;
    if (d->height == 0 || d->height == INT32_MIN) return BMP_ERR_BAD_GEOMETRY;
    if (d->planes != 1) return BMP_ERR_BAD_GEOMETRY;

    int w = d->width;
    int h = (d->height < 0) ? -d->height : d->height;

    size_t npix;
    if (!mul_ok((size_t)w, (size_t)h, &npix)) return BMP_ERR_TOO_MANY_PIXELS;
    if (npix > PIXLAB_MAX_PIXELS) return BMP_ERR_TOO_MANY_PIXELS;

    size_t bits, s;
    if (!mul_ok((size_t)w, (size_t)d->bpp, &bits)) return BMP_ERR_TOO_MANY_PIXELS;
    s = ((bits + 31) / 32) * 4; // 每行按 4 字节对齐

    if ((size_t)off_bits < 14u + (size_t)d->header_size) return BMP_ERR_BAD_OFFSET;
    if ((size_t)off_bits > file_size) return BMP_ERR_BAD_OFFSET;

    if (d->compression == 1 || d->compression == 2) {
        if ((size_t)off_bits >= file_size) return BMP_ERR_TOO_SHORT;
    } else {
        size_t need;
        if (!mul_ok(s, (size_t)h, &need)) return BMP_ERR_TOO_MANY_PIXELS;
        if ((size_t)off_bits + need > file_size) return BMP_ERR_TOO_SHORT;
    }

    *aw = w;
    *ah = h;
    *top_down = (d->height < 0);
    *stride = s;
    return BMP_OK;
}

/* ============================ 非压缩解码 ============================ */

/* 解一行原始扫描线到目标行。out_ch 为 1 或 3。 */
static void unpack_row(const unsigned char* src, const BmpDib* d,
                       const BmpPalette* pal, const BmpMasks* m, int w,
                       unsigned char* dst, int out_ch) {
    for (int x = 0; x < w; x++) {
        unsigned char r = 0, g = 0, b = 0;

        switch (d->bpp) {
        case 1: {
            int idx = (src[x >> 3] >> (7 - (x & 7))) & 1; // 最高位在左
            if (idx >= pal->count) idx = 0;
            r = pal->rgb[idx][0];
            g = pal->rgb[idx][1];
            b = pal->rgb[idx][2];
            break;
        }
        case 4: {
            unsigned char byte = src[x >> 1];
            int idx = (x & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F); // 高半字节在左
            if (idx >= pal->count) idx = 0;
            r = pal->rgb[idx][0];
            g = pal->rgb[idx][1];
            b = pal->rgb[idx][2];
            break;
        }
        case 8: {
            int idx = src[x];
            if (idx >= pal->count) idx = 0; // 索引越界回退为 0，绝不越界读表
            r = pal->rgb[idx][0];
            g = pal->rgb[idx][1];
            b = pal->rgb[idx][2];
            break;
        }
        case 16: {
            uint32_t v = (uint32_t)src[(size_t)x * 2] |
                         ((uint32_t)src[(size_t)x * 2 + 1] << 8);
            r = mask_value(v, &m->r);
            g = mask_value(v, &m->g);
            b = mask_value(v, &m->b);
            break;
        }
        case 24:
            b = src[(size_t)x * 3 + 0];
            g = src[(size_t)x * 3 + 1];
            r = src[(size_t)x * 3 + 2];
            break;
        default: // 32：第 4 字节按规范是保留位，一律丢弃
            b = src[(size_t)x * 4 + 0];
            g = src[(size_t)x * 4 + 1];
            r = src[(size_t)x * 4 + 2];
            break;
        }

        if (out_ch == 1) {
            dst[x] = luma_u8(r, g, b);
        } else {
            unsigned char* o = dst + (size_t)x * 3;
            o[0] = r;
            o[1] = g;
            o[2] = b;
        }
    }
}

static BmpError decode_uncompressed(const BmpReader* r, size_t off_bits,
                                    const BmpDib* d, const BmpPalette* pal,
                                    const BmpMasks* m, int w, int h, int top_down,
                                    size_t stride, Image* img) {
    for (int y = 0; y < h; y++) {
        size_t row_off = off_bits + (size_t)y * stride;
        int dy = top_down ? y : (h - 1 - y);
        unpack_row(r->buf + row_off, d, pal, m, w, image_at(img, dy, 0),
                   img->channels);
    }
    return BMP_OK;
}

/* ============================ RLE 解码 ============================
   先解成 W×H 索引平面，再复用同一套展开逻辑 —— 方向翻转、调色板查表、
   灰度判定全部共享。注意绝对模式按"字（2 字节）"对齐，不是 4 字节行对齐。 */

static void rle_put(unsigned char* idx, int w, int h, int x, int y,
                    unsigned char v) {
    if (x < 0 || x >= w || y < 0 || y >= h) return; // 越界丢弃，绝不越界写
    idx[(size_t)y * (size_t)w + (size_t)x] = v;
}

static BmpError rle8_to_plane(BmpReader* r, size_t off, int w, int h,
                              unsigned char* idx) {
    if (!br_seek(r, off)) return BMP_ERR_TOO_SHORT;
    int x = 0, y = 0;

    for (;;) {
        unsigned char cnt, val;
        if (!br_u8(r, &cnt) || !br_u8(r, &val)) return BMP_OK; // 码流耗尽=正常结束

        if (cnt > 0) { // 编码模式
            // run 跨越行尾属于数据损坏（规范禁止）。静默截断会让坏文件
            // 解出一张看似正常的垃圾图，用户无从察觉，所以宁可报错。
            if (x + cnt > w) return BMP_ERR_RLE_CORRUPT;
            for (int i = 0; i < cnt; i++, x++) rle_put(idx, w, h, x, y, val);
        } else if (val == 0) { // 行结束
            x = 0;
            if (++y >= h) return BMP_OK;
        } else if (val == 1) { // 位图结束
            return BMP_OK;
        } else if (val == 2) { // delta
            unsigned char dx, dy;
            if (!br_u8(r, &dx) || !br_u8(r, &dy)) return BMP_OK;
            x += dx;
            y += dy;
            if (x > w || y >= h) return BMP_ERR_RLE_CORRUPT;
        } else { // 绝对模式：val 个字面像素
            int n = val;
            if (x + n > w) return BMP_ERR_RLE_CORRUPT;
            for (int i = 0; i < n; i++) {
                unsigned char p;
                if (!br_u8(r, &p)) return BMP_OK;
                rle_put(idx, w, h, x, y, p);
                x++;
            }
            if (n & 1) { // 按 2 字节对齐
                if (!br_skip(r, 1)) return BMP_OK;
            }
        }
    }
}

static BmpError rle4_to_plane(BmpReader* r, size_t off, int w, int h,
                              unsigned char* idx) {
    if (!br_seek(r, off)) return BMP_ERR_TOO_SHORT;
    int x = 0, y = 0;

    for (;;) {
        unsigned char cnt, val;
        if (!br_u8(r, &cnt) || !br_u8(r, &val)) return BMP_OK;

        if (cnt > 0) { // 编码模式：高半字节在前交替取
            // 同 RLE8：run 跨行即损坏，报错而非静默截断。
            if (x + cnt > w) return BMP_ERR_RLE_CORRUPT;
            for (int i = 0; i < cnt; i++, x++) {
                int nib = (i & 1) ? (val & 0x0F) : ((val >> 4) & 0x0F);
                rle_put(idx, w, h, x, y, (unsigned char)nib);
            }
        } else if (val == 0) {
            x = 0;
            if (++y >= h) return BMP_OK;
        } else if (val == 1) {
            return BMP_OK;
        } else if (val == 2) {
            unsigned char dx, dy;
            if (!br_u8(r, &dx) || !br_u8(r, &dy)) return BMP_OK;
            x += dx;
            y += dy;
            if (x > w || y >= h) return BMP_ERR_RLE_CORRUPT;
        } else { // 绝对模式：val 个像素打包进 (val+1)/2 字节
            int n = val;
            int bytes = (n + 1) / 2;
            if (x + n > w) return BMP_ERR_RLE_CORRUPT;
            unsigned char cur = 0;
            for (int i = 0; i < n; i++) {
                if ((i & 1) == 0) {
                    if (!br_u8(r, &cur)) return BMP_OK;
                }
                int nib = (i & 1) ? (cur & 0x0F) : ((cur >> 4) & 0x0F);
                rle_put(idx, w, h, x, y, (unsigned char)nib);
                x++;
            }
            if (bytes & 1) {
                if (!br_skip(r, 1)) return BMP_OK;
            }
        }
    }
}

static BmpError decode_rle(BmpReader* r, size_t off_bits, const BmpDib* d,
                           const BmpPalette* pal, int w, int h, int top_down,
                           Image* img) {
    size_t npix;
    if (!mul_ok((size_t)w, (size_t)h, &npix)) return BMP_ERR_TOO_MANY_PIXELS;

    unsigned char* idx = calloc(npix, 1);
    if (!idx) return BMP_ERR_OOM;

    BmpError e = (d->bpp == 8) ? rle8_to_plane(r, off_bits, w, h, idx)
                               : rle4_to_plane(r, off_bits, w, h, idx);
    if (e != BMP_OK) {
        free(idx);
        return e;
    }

    for (int y = 0; y < h; y++) {
        int dy = top_down ? y : (h - 1 - y);
        const unsigned char* row = idx + (size_t)y * (size_t)w;
        unsigned char* dst = image_at(img, dy, 0);
        for (int x = 0; x < w; x++) {
            int i = row[x];
            if (i >= pal->count) i = 0;
            if (img->channels == 1) {
                dst[x] = pal->rgb[i][0];
            } else {
                unsigned char* o = dst + (size_t)x * 3;
                o[0] = pal->rgb[i][0];
                o[1] = pal->rgb[i][1];
                o[2] = pal->rgb[i][2];
            }
        }
    }
    free(idx);
    return BMP_OK;
}

/* ============================ 公开 API ============================ */

Image* bmp_load_ex(const char* path, BmpError* err) {
    BmpError e = BMP_OK;
    unsigned char* buf = NULL;
    size_t len = 0;
    Image* img = NULL;

    if (!read_entire_file(path, PIXLAB_MAX_FILE, &buf, &len)) {
        if (err) *err = BMP_ERR_OPEN;
        return NULL;
    }

    BmpReader r = {buf, len, 0};

    unsigned char sig[2];
    uint32_t off_bits = 0;
    uint16_t res1 = 0, res2 = 0;
    {
        uint32_t fsize;
        if (!br_read(&r, sig, 2)) {
            e = BMP_ERR_TOO_SHORT;
            goto done;
        }
        if (sig[0] != 'B' || sig[1] != 'M') {
            e = BMP_ERR_NOT_BMP;
            goto done;
        }
        // bfSize 不可信，读掉但不用；一切以真实文件长度为准
        if (!br_u32(&r, &fsize) || !br_u16(&r, &res1) || !br_u16(&r, &res2) ||
            !br_u32(&r, &off_bits)) {
            e = BMP_ERR_TOO_SHORT;
            goto done;
        }
    }

    BmpDib d;
    BmpMasks masks;
    BmpPalette pal;
    int w, h, top_down;
    size_t stride;

    if ((e = parse_dib(&r, &d)) != BMP_OK) goto done;
    if ((e = load_masks(&r, &d, &masks)) != BMP_OK) goto done;
    if ((e = load_palette(&r, &d, off_bits, &pal)) != BMP_OK) goto done;
    if ((e = validate(&d, off_bits, len, &w, &h, &top_down, &stride)) != BMP_OK)
        goto done;

    // 调色板图若是纯灰阶，直接给 1 通道 —— 省 2/3 内存，且能走字符画预览
    int out_ch = (d.bpp <= 8 && pal.is_gray) ? 1 : 3;

    img = image_new(w, h, out_ch);
    if (!img) {
        e = BMP_ERR_OOM;
        goto done;
    }

    img->bmp.valid = 1;
    img->bmp.xppm = (d.xppm > 0) ? (uint32_t)d.xppm : 0;
    img->bmp.yppm = (d.yppm > 0) ? (uint32_t)d.yppm : 0;
    img->bmp.reserved1 = res1;
    img->bmp.reserved2 = res2;

    if (d.compression == 1 || d.compression == 2) {
        e = decode_rle(&r, off_bits, &d, &pal, w, h, top_down, img);
    } else {
        e = decode_uncompressed(&r, off_bits, &d, &pal, &masks, w, h, top_down,
                                stride, img);
    }
    if (e != BMP_OK) {
        image_free(img);
        img = NULL;
    }

done:
    free(buf);
    if (err) *err = e;
    return img;
}

Image* bmp_load(const char* path) {
    return bmp_load_ex(path, NULL);
}

/* -------- 写出 -------- */

static void put_u16(FILE* fp, uint16_t v) {
    fputc((int)(v & 0xFF), fp);
    fputc((int)((v >> 8) & 0xFF), fp);
}

static void put_u32(FILE* fp, uint32_t v) {
    fputc((int)(v & 0xFF), fp);
    fputc((int)((v >> 8) & 0xFF), fp);
    fputc((int)((v >> 16) & 0xFF), fp);
    fputc((int)((v >> 24) & 0xFF), fp);
}

int bmp_save(const char* path, const Image* img) {
    if (!img || img->width <= 0 || img->height <= 0) return -1;
    if (img->channels != 1 && img->channels != 3) return -1;

    int w = img->width, h = img->height, ch = img->channels;

    // 输出恒为 24 位：无论输入是几个通道、什么格式
    size_t row_bytes = (size_t)w * 3;
    size_t row_padded = (row_bytes + 3) & ~(size_t)3;
    size_t pixel_bytes;
    if (!mul_ok(row_padded, (size_t)h, &pixel_bytes)) return -1;
    if (pixel_bytes > 0xFFFFFFFFu - 54u) return -1;

    FILE* fp = fopen(path, "wb");
    if (!fp) return -1;

    // 文件头
    fputc('B', fp);
    fputc('M', fp);
    put_u32(fp, (uint32_t)(54 + pixel_bytes));
    put_u16(fp, img->bmp.reserved1);
    put_u16(fp, img->bmp.reserved2);
    put_u32(fp, 54);

    // 信息头：恒定 40 字节、BI_RGB、自底向上（最大兼容性）
    put_u32(fp, 40);
    put_u32(fp, (uint32_t)w);
    put_u32(fp, (uint32_t)h);
    put_u16(fp, 1);
    put_u16(fp, 24);
    put_u32(fp, 0);
    put_u32(fp, (uint32_t)pixel_bytes); // 按实际重算，不沿用输入值
    put_u32(fp, img->bmp.xppm);
    put_u32(fp, img->bmp.yppm);
    put_u32(fp, 0);
    put_u32(fp, 0);

    // 像素：自底向上，每像素 BGR
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            const unsigned char* p = image_at(img, y, x);
            unsigned char r, g, b;
            if (ch == 1) {
                r = g = b = p[0];
            } else {
                r = p[0];
                g = p[1];
                b = p[2];
            }
            fputc((int)b, fp);
            fputc((int)g, fp);
            fputc((int)r, fp);
        }
        for (size_t k = row_bytes; k < row_padded; k++) fputc(0, fp);
    }

    if (ferror(fp)) {
        fclose(fp);
        return -1;
    }
    return (fclose(fp) == 0) ? 0 : -1;
}

/* -------- 头部探查（info 子命令用） -------- */

BmpError bmp_probe(const char* path, BmpInfo* out) {
    unsigned char* buf = NULL;
    size_t len = 0;
    if (!read_entire_file(path, PIXLAB_MAX_FILE, &buf, &len)) return BMP_ERR_OPEN;

    BmpReader r = {buf, len, 0};
    BmpError e = BMP_OK;
    memset(out, 0, sizeof(*out));
    out->file_size = len;

    unsigned char sig[2];
    uint32_t off_bits = 0;
    if (!br_read(&r, sig, 2)) {
        e = BMP_ERR_TOO_SHORT;
        goto done;
    }
    if (sig[0] != 'B' || sig[1] != 'M') {
        e = BMP_ERR_NOT_BMP;
        goto done;
    }
    {
        uint32_t fsize;
        uint16_t r1, r2;
        if (!br_u32(&r, &fsize) || !br_u16(&r, &r1) || !br_u16(&r, &r2) ||
            !br_u32(&r, &off_bits)) {
            e = BMP_ERR_TOO_SHORT;
            goto done;
        }
    }
    out->off_bits = off_bits;

    BmpDib d;
    BmpMasks masks;
    BmpPalette pal;
    int w, h, top_down;
    size_t stride;

    if ((e = parse_dib(&r, &d)) != BMP_OK) goto done;
    if ((e = load_masks(&r, &d, &masks)) != BMP_OK) goto done;
    if ((e = load_palette(&r, &d, off_bits, &pal)) != BMP_OK) goto done;
    if ((e = validate(&d, off_bits, len, &w, &h, &top_down, &stride)) != BMP_OK)
        goto done;

    out->header_size = d.header_size;
    out->width = d.width;
    out->height = d.height;
    out->top_down = top_down;
    out->planes = d.planes;
    out->bpp = d.bpp;
    out->compression = d.compression;
    out->size_image = d.size_image;
    out->xppm = d.xppm;
    out->yppm = d.yppm;
    out->clr_used = d.clr_used;
    out->clr_important = d.clr_important;
    out->palette_entries = pal.count;
    out->has_masks = masks.active;
    out->mask_r = masks.r.mask;
    out->mask_g = masks.g.mask;
    out->mask_b = masks.b.mask;
    out->mask_a = masks.a.mask;
    out->stride = stride;

done:
    free(buf);
    return e;
}

/* -------- 文案 -------- */

const char* bmp_strerror(BmpError e) {
    switch (e) {
    case BMP_OK: return "成功";
    case BMP_ERR_OPEN: return "无法打开文件（不存在、无权限、为空或超过 256MB）";
    case BMP_ERR_TOO_SHORT: return "文件被截断，头部或像素数据不完整";
    case BMP_ERR_NOT_BMP: return "不是 BMP 文件（缺少 \"BM\" 标识）";
    case BMP_ERR_BAD_DIB: return "DIB 头尺寸非法（无法识别的 biSize）";
    case BMP_ERR_BAD_GEOMETRY: return "图像尺寸或压缩方式与位深不匹配";
    case BMP_ERR_UNSUPPORTED_BPP: return "不支持的位深（仅支持 1/4/8/16/24/32）";
    case BMP_ERR_COMPRESSION_JPEG: return "不支持内嵌 JPEG 的 BMP";
    case BMP_ERR_COMPRESSION_PNG: return "不支持内嵌 PNG 的 BMP";
    case BMP_ERR_COMPRESSION_UNKNOWN: return "无法识别的压缩方式";
    case BMP_ERR_BAD_PALETTE: return "调色板区域不可用";
    case BMP_ERR_BAD_MASK: return "位掩码非法（为零、重叠或不连续）";
    case BMP_ERR_BAD_OFFSET: return "像素数据偏移（bfOffBits）落在非法区间";
    case BMP_ERR_RLE_CORRUPT: return "RLE 压缩数据损坏";
    case BMP_ERR_TOO_MANY_PIXELS: return "图像过大（超过一亿像素）";
    case BMP_ERR_OOM: return "内存不足";
    default: return "未知错误";
    }
}

const char* bmp_header_name(uint32_t header_size) {
    switch (header_size) {
    case 12: return "BITMAPCOREHEADER (OS/2 v1)";
    case 40: return "BITMAPINFOHEADER";
    case 52: return "BITMAPV2INFOHEADER";
    case 56: return "BITMAPV3INFOHEADER";
    case 64: return "OS/2 v2";
    case 108: return "BITMAPV4HEADER";
    case 124: return "BITMAPV5HEADER";
    default: return "未知头类型";
    }
}

const char* bmp_compression_name(uint32_t compression) {
    switch (compression) {
    case 0: return "BI_RGB（无压缩）";
    case 1: return "BI_RLE8";
    case 2: return "BI_RLE4";
    case 3: return "BI_BITFIELDS";
    case 4: return "BI_JPEG";
    case 5: return "BI_PNG";
    case 6: return "BI_ALPHABITFIELDS";
    default: return "未知";
    }
}
