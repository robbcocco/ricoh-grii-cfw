/* memset / memcpy for compiler-generated calls (struct copies and
 * initialisers). The payload links without a C library; the Makefile's
 * -fno-tree-loop-distribute-patterns keeps these loops from calling
 * themselves. */
#include <stddef.h>

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
