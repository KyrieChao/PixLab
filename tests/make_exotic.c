/* make_exotic.c — 构造 System.Drawing 生成不了的 BMP 变体。
 *
 * GDI+ 只会写 40 字节头的常规 BMP。RLE 压缩、OS/2 CORE 头、V4/V5 头、
 * 自顶向下这些只能自己按字节拼。每个样本的像素内容都是显式写死的，
 * 解码后用 pixlab info 的校验和与 System.Drawing 读回的结果对照。
 *
 * 构建：gcc -std=c99 -Wall -Wextra -O2 -o tests/make_exotic.exe tests/make_exotic.c
 * 运行：先建好输出目录，再把目录作为第一个参数传入（默认 samples/exotic）
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 16
#define H 8

/* 行宽（本文件的尺寸下都恰好 4 字节对齐，省掉填充） */
#define STRIDE24 ((W * 3 + 3) / 4 * 4)
#define STRIDE16 ((W * 2 + 3) / 4 * 4)
#define STRIDE8  ((W * 1 + 3) / 4 * 4)
#define STRIDE4  ((W / 2 + 3) / 4 * 4)

static void w16(FILE* f, uint16_t v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void w32(FILE* f, uint32_t v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

/* 文件头。bfSize 写真实值。 */
static void file_header(FILE* f, uint32_t off_bits, uint32_t file_size) {
    fputc('B', f);
    fputc('M', f);
    w32(f, file_size);
    w16(f, 0);
    w16(f, 0);
    w32(f, off_bits);
}

/* DIB 头的公共字段（36 字节，不含开头的 biSize）。
   V4/V5 只是在这个基础上追加字段，所以把 biSize 留给调用方先写。 */
static void info_fields(FILE* f, int32_t h, uint16_t bpp, uint32_t compression,
                        uint32_t size_image, uint32_t clr_used) {
    w32(f, (uint32_t)W);
    w32(f, (uint32_t)h);
    w16(f, 1);
    w16(f, bpp);
    w32(f, compression);
    w32(f, size_image);
    w32(f, 2835); /* xppm */
    w32(f, 2835); /* yppm */
    w32(f, clr_used);
    w32(f, 0);
}

/* 标准 40 字节 BITMAPINFOHEADER */
static void info_header(FILE* f, int32_t h, uint16_t bpp, uint32_t compression,
                        uint32_t size_image, uint32_t clr_used) {
    w32(f, 40);
    info_fields(f, h, bpp, compression, size_image, clr_used);
}

/* 大家共用的彩色图案，保证每个样本的"正确答案"是同一条公式 */
static void pattern_rgb(int x, int y, unsigned char* o) {
    o[0] = (unsigned char)(x * 255 / (W - 1));
    o[1] = (unsigned char)(y * 255 / (H - 1));
    o[2] = (unsigned char)((x + y) * 255 / (W + H - 2));
}

/* 按 BMP 的行序（默认自底向上）写 24 位像素 */
static void write_pixels24(FILE* f, int top_down) {
    for (int i = 0; i < H; i++) {
        int y = top_down ? i : (H - 1 - i);
        for (int x = 0; x < W; x++) {
            unsigned char c[3];
            pattern_rgb(x, y, c);
            fputc(c[2], f); /* B */
            fputc(c[1], f); /* G */
            fputc(c[0], f); /* R */
        }
    }
}

/* ---------- 1. OS/2 v1 CORE 头（12 字节，宽高为无符号 16 位） ---------- */
static void gen_core24(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/core24.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t off = 14 + 12;
    file_header(f, off, off + STRIDE24 * H);

    w32(f, 12); /* biSize */
    w16(f, W);  /* 宽：CORE 头是无符号 16 位 */
    w16(f, H);  /* 高：同上，没有自顶向下的说法 */
    w16(f, 1);
    w16(f, 24);

    write_pixels24(f, 0);
    fclose(f);
    printf("  core24.bmp    OS/2 v1 CORE 头 (12 字节) + 24 位\n");
}

/* ---------- 2. BITMAPV4HEADER（108 字节） ---------- */
static void gen_v4(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/v4.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t off = 14 + 108;
    file_header(f, off, off + STRIDE24 * H);

    w32(f, 108); /* biSize：V4 头 */
    info_fields(f, H, 24, 0, STRIDE24 * H, 0);
    /* V4 追加：掩码(16) + CSType(4) + 端点(36) + gamma(12) = 68 字节 */
    w32(f, 0x00FF0000); /* 24 位下掩码不参与解码，但真实文件里会有 */
    w32(f, 0x0000FF00);
    w32(f, 0x000000FF);
    w32(f, 0x00000000);
    w32(f, 0x73524742); /* CSType = 'BGRs' */
    for (int i = 0; i < 9; i++) w32(f, 0); /* CIEXYZ 端点 */
    w32(f, 0); /* gamma R */
    w32(f, 0); /* gamma G */
    w32(f, 0); /* gamma B */

    write_pixels24(f, 0);
    fclose(f);
    printf("  v4.bmp        BITMAPV4HEADER (108 字节)\n");
}

/* ---------- 3. BITMAPV5HEADER（124 字节） ---------- */
static void gen_v5(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/v5.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t off = 14 + 124;
    file_header(f, off, off + STRIDE24 * H);

    w32(f, 124); /* biSize：V5 头 */
    info_fields(f, H, 24, 0, STRIDE24 * H, 0);
    w32(f, 0x00FF0000);
    w32(f, 0x0000FF00);
    w32(f, 0x000000FF);
    w32(f, 0x00000000);
    w32(f, 0x73524742);
    for (int i = 0; i < 9; i++) w32(f, 0);
    w32(f, 0);
    w32(f, 0);
    w32(f, 0);
    /* V5 追加 16 字节 */
    w32(f, 4); /* bV5Intent = LCS_GM_IMAGES */
    w32(f, 0); /* profile data 偏移 */
    w32(f, 0); /* profile size */
    w32(f, 0); /* reserved */

    write_pixels24(f, 0);
    fclose(f);
    printf("  v5.bmp        BITMAPV5HEADER (124 字节)\n");
}

/* ---------- 4. 自顶向下（biHeight 为负） ---------- */
static void gen_topdown(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/topdown.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t off = 14 + 40;
    file_header(f, off, off + STRIDE24 * H);
    info_header(f, -H, 24, 0, STRIDE24 * H, 0); /* 负高度 */
    write_pixels24(f, 1);
    fclose(f);
    printf("  topdown.bmp   biHeight = -8（自顶向下）\n");
}

/* ---------- 5. 40 字节头 + BI_BITFIELDS + 显式 5-6-5 掩码 ---------- */
static void gen_bf565(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/bf565.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t off = 14 + 40 + 12; /* 掩码紧跟在头之后 */
    file_header(f, off, off + STRIDE16 * H);
    info_header(f, H, 16, 3 /* BI_BITFIELDS */, STRIDE16 * H, 0);

    w32(f, 0xF800); /* R 5 位 */
    w32(f, 0x07E0); /* G 6 位 */
    w32(f, 0x001F); /* B 5 位 */

    for (int i = 0; i < H; i++) {
        int y = H - 1 - i;
        for (int x = 0; x < W; x++) {
            unsigned char c[3];
            pattern_rgb(x, y, c);
            uint16_t v = (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) |
                                    (c[2] >> 3));
            w16(f, v);
        }
    }
    fclose(f);
    printf("  bf565.bmp     40 头 + BI_BITFIELDS + 显式 565 掩码\n");
}

/* ---------- 6. RLE8 ---------- */
/* 索引图案（含长 run、绝对模式、行结束、位图结束）：
     行7: 16 个 96      行6: 16 个 200     行5: 8 个 0 + 8 个 255
     行4: 16 个 32      行3: 16 个 255     行2: 0,16,32..240 递增
     行1: 8 个 64 + 8 个 128                行0: 16 个 0 */
static const unsigned char RLE8_ROW[8][W] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {64, 64, 64, 64, 64, 64, 64, 64, 128, 128, 128, 128, 128, 128, 128, 128},
    {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240},
    {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
    {32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32},
    {0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255},
    {200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200},
    {96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96},
};

static void gen_rle8(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/rle8.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t off = 14 + 40 + 256 * 4;
    file_header(f, off, off + 200); /* 大小写个够用的值，解码不看它 */
    info_header(f, H, 8, 1 /* BI_RLE8 */, 200, 0);

    for (int i = 0; i < 256; i++) { /* 256 级灰阶调色板 */
        fputc(i, f);
        fputc(i, f);
        fputc(i, f);
        fputc(0, f);
    }

    /* 自底向上：先写行 7 */
    static const unsigned char rows_bottom_up[8] = {7, 6, 5, 4, 3, 2, 1, 0};
    for (int r = 0; r < 8; r++) {
        const unsigned char* row = RLE8_ROW[rows_bottom_up[r]];
        int x = 0;
        while (x < W) {
            int run = 1;
            while (x + run < W && row[x + run] == row[x] && run < 255) run++;
            fputc(run, f);       /* count */
            fputc(row[x], f);    /* value */
            x += run;
        }
        fputc(0, f); /* 行结束 */
        fputc(0, f);
    }
    fputc(0, f); /* 位图结束 */
    fputc(1, f);

    fclose(f);
    printf("  rle8.bmp      8bpp + BI_RLE8\n");
}

/* ---------- 7. RLE4 ---------- */
/* 行7:全 3  行6:全 9  行5:8个1+8个2  行4:交替 4,F
   行3:0..15 递增      行2:全 8  行1:全 6  行0:全 2 */
static void gen_rle4(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/rle4.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t off = 14 + 40 + 16 * 4;
    file_header(f, off, off + 200);
    info_header(f, H, 4, 2 /* BI_RLE4 */, 200, 0);

    for (int i = 0; i < 16; i++) { /* 16 级灰阶：i*17 铺满 0..255 */
        int g = i * 17;
        fputc(g, f);
        fputc(g, f);
        fputc(g, f);
        fputc(0, f);
    }

    /* 自底向上。每行用 RLE4 的 (count, 半字节值) 表示 */
    static const unsigned char nib_row[8][W] = {
        {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},      /* 行0 */
        {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},      /* 行1 */
        {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},      /* 行2 */
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},/* 行3 */
        {4, 15, 4, 15, 4, 15, 4, 15, 4, 15, 4, 15, 4, 15, 4, 15}, /* 行4 */
        {1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2},      /* 行5 */
        {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9},      /* 行6 */
        {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3},      /* 行7 */
    };
    static const unsigned char rows_bottom_up[8] = {7, 6, 5, 4, 3, 2, 1, 0};

    for (int r = 0; r < 8; r++) {
        const unsigned char* row = nib_row[rows_bottom_up[r]];
        int x = 0;
        while (x < W) {
            int run = 1;
            /* 交替图案（如 4,15,4,15）也按 run 处理：RLE4 的 run 会让
               高/低半字节交替取，所以 0x4F 正好就是 4,15,4,15... */
            while (x + run < W && run < 255 && row[x + run] == row[x + (run & 1)])
                run++;
            unsigned char v = (unsigned char)((row[x] << 4) | row[x + (run > 1 ? 1 : 0)]);
            fputc(run, f);
            fputc(v, f);
            x += run;
        }
        fputc(0, f);
        fputc(0, f);
    }
    fputc(0, f);
    fputc(1, f);

    fclose(f);
    printf("  rle4.bmp      4bpp + BI_RLE4\n");
}

/* ---------- 8. 畸形样本：都应当被清晰拒绝，且不崩溃 ---------- */

static void gen_bad_bpp(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/bad_bpp.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t off = 14 + 40;
    file_header(f, off, off + 100);
    info_header(f, H, 48, 0, 0, 0); /* 位深 48 不存在 */
    for (int i = 0; i < 100; i++) fputc(0, f);
    fclose(f);
    printf("  bad_bpp.bmp   位深 48（应报错）\n");
}

static void gen_truncated(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/truncated.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t off = 14 + 40;
    file_header(f, off, off + STRIDE24 * H); /* 声称有整幅像素 */
    info_header(f, H, 24, 0, STRIDE24 * H, 0);
    for (int i = 0; i < 20; i++) fputc(0x7F, f); /* 实际只给 20 字节 */
    fclose(f);
    printf("  truncated.bmp 头声称整幅、数据只给 20 字节（应报错）\n");
}

static void gen_fake_rle(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/fake_rle.bmp", dir);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t off = 14 + 40 + 256 * 4;
    file_header(f, off, off + 64);
    info_header(f, H, 8, 1, 64, 0);
    for (int i = 0; i < 256; i++) {
        fputc(i, f);
        fputc(i, f);
        fputc(i, f);
        fputc(0, f);
    }
    /* 一段永远不结束、且不断要求写满整行的码流 */
    for (int i = 0; i < 30; i++) {
        fputc(0xFF, f);
        fputc(0x11, f);
    }
    fclose(f);
    printf("  fake_rle.bmp  伪造的无限 RLE 流（应安全截断）\n");
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : "samples/exotic";
    printf("生成 exotic 样本到 %s/\n", dir);

    gen_core24(dir);
    gen_v4(dir);
    gen_v5(dir);
    gen_topdown(dir);
    gen_bf565(dir);
    gen_rle8(dir);
    gen_rle4(dir);
    gen_bad_bpp(dir);
    gen_truncated(dir);
    gen_fake_rle(dir);

    printf("完成\n");
    return 0;
}
