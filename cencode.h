#ifndef CENCODE_H
#define CENCODE_H
#include <stddef.h>
int c_encode(char *dst, size_t dstmax, const char *src, size_t srclen);
int c_decode(char *dst, size_t dstmax, const char *src, size_t srclen);
#endif
