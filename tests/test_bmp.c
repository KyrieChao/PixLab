/* BMP 往返锚点测试。
 *
 * 读一张 24 位 BMP → 原样写回 → 再读回 → 逐像素比对。
 * 编码器和解码器互为逆运算，任何一侧改动破坏了这一点都会在这里报出来。
 *
 * 构建：make tests/test_bmp.exe
 * 运行：在项目根目录执行 tests/test_bmp.exe
 */

#include "bmp.h"
#include "image.h"

#include <stdio.h>

int main(void) {
    const char* in_path = "tests/fixtures/test.bmp";
    const char* out_path = "samples/roundtrip.bmp";

    BmpError e;
    Image* a = bmp_load_ex(in_path, &e);
    if (!a) {
        printf("FAIL  无法读取 %s：%s\n", in_path, bmp_strerror(e));
        return 1;
    }

    if (bmp_save(out_path, a) != 0) {
        printf("FAIL  无法写出 %s\n", out_path);
        image_free(a);
        return 1;
    }

    Image* b = bmp_load_ex(out_path, &e);
    if (!b) {
        printf("FAIL  无法读回 %s：%s\n", out_path, bmp_strerror(e));
        image_free(a);
        return 1;
    }

    int ok = (a->width == b->width) && (a->height == b->height) &&
             (a->channels == b->channels);

    size_t diff = 0;
    if (ok) {
        size_t n = (size_t)a->width * (size_t)a->height * (size_t)a->channels;
        for (size_t i = 0; i < n; i++) {
            if (a->data[i] != b->data[i]) diff++;
        }
        if (diff) ok = 0;
    }

    printf("%s  往返 %s：%d x %d, %d 通道%s\n", ok ? "PASS" : "FAIL", in_path,
           a->width, a->height, a->channels,
           diff ? "（有字节不一致）" : "，逐像素一致");

    image_free(a);
    image_free(b);
    return ok ? 0 : 1;
}
