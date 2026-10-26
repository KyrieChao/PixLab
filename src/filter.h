#ifndef FILTER_H
#define FILTER_H

#include "image.h"

/* 一个滤镜的描述。
   滤镜自己分配并返回输出图，CLI 只负责查表调用和释放 ——
   于是"新增滤镜"就是往表里加一行，不必再动 main.c 里的 if 链。 */
typedef struct {
    const char* name;
    const char* help;
    int         has_arg;  // 是否接受参数（写法 name:参数）
    double      def_arg;  // 省略参数时的默认值
    Image* (*apply)(const Image* src, double arg);
} Filter;

/* 全部滤镜，以 name == NULL 结尾。 */
const Filter* filter_table(void);

/* 按名字查找，找不到返回 NULL。 */
const Filter* filter_find(const char* name);

#endif
