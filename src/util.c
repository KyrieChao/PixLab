#include "util.h"

#include <stdio.h>
#include <stdlib.h>

int mul_ok(size_t a, size_t b, size_t* out) {
    if (a != 0 && b > (size_t)-1 / a) return 0; // 乘回去不相等即溢出
    *out = a * b;
    return 1;
}

int read_entire_file(const char* path, size_t max_bytes,
                     unsigned char** out, size_t* len) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return 0;

    // 先问大小，避免边读边增长
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    long end = ftell(fp);
    if (end <= 0 || (unsigned long)end > max_bytes) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    size_t n = (size_t)end;
    unsigned char* buf = malloc(n);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    // 一次读满；短读说明文件在 ftell 之后被截断
    if (fread(buf, 1, n, fp) != n) {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    *out = buf;
    *len = n;
    return 1;
}
