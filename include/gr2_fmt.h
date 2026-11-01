#ifndef GR2_FMT_H
#define GR2_FMT_H
#include <stdint.h>

/* Formatting without division: ARMv5TE has no divide instruction and the
 * payload links without libgcc (__aeabi_idiv). */

static inline char *put_str(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

static inline char *put_hex32(char *p, uint32_t v) {
    static const char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) *p++ = digits[(v >> shift) & 0xF];
    return p;
}

/* a / b by shift-and-subtract (b != 0). */
static inline uint32_t udiv32(uint32_t a, uint32_t b) {
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { r -= b; q |= 1u << i; }
    }
    return q;
}

static inline char *put_udec(char *p, uint32_t v) {
    static const uint32_t pow10[] = { 1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
                                      10000u, 1000u, 100u, 10u, 1u };
    int started = 0;
    for (int i = 0; i < 10; i++) {
        char d = '0';
        while (v >= pow10[i]) { v -= pow10[i]; d++; }
        if (d != '0' || started || i == 9) { *p++ = d; started = 1; }
    }
    return p;
}

static inline char *put_sdec(char *p, int32_t v) {
    if (v < 0) { *p++ = '-'; v = -v; }
    return put_udec(p, (uint32_t)v);
}

#endif /* GR2_FMT_H */
