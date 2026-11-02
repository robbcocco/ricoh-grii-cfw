/* Two-generation records on D: (include/store.h, finding 34 §7). The stock file
 * API (File_Open "w" creates or truncates) writes one file per save; D:/CFW is
 * created once with File_Mkdir. Reads and writes go through File_Open/Read/
 * Write/Close only, so the stack needs are those of finding 31. */
#include <stdint.h>
#include "store.h"
#include "gr2_hw.h"

#define FILE_READ  0x00
#define FILE_WRITE 0x01
#define DIR_PATH   "D:/CFW"

typedef struct { uint32_t magic, gen, len, crc; } hdr_t;

/* CRC-32 (IEEE, reflected), bitwise: no table, no division. */
static uint32_t crc32(uint32_t c, const uint8_t *p, uint32_t n) {
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static uint32_t record_crc(uint32_t gen, uint32_t len, const void *data) {
    uint32_t c = crc32(0, (const uint8_t *)&gen, 4);
    c = crc32(c, (const uint8_t *)&len, 4);
    return crc32(c, data, len);
}

/* File i into buf: its generation in *gen and the payload length, or -1. */
static int read_one(const store_t *s, int i, void *buf, uint32_t max, uint32_t *gen) {
    int fd = GR2_FN_FILE_OPEN(s->path[i], FILE_READ);
    if (fd == 0) return -1;
    hdr_t h;
    uint8_t extra;
    int ok = GR2_FN_FILE_READ(fd, &h, sizeof(h)) == (int)sizeof(h) && h.magic == s->magic && h.len <= max &&
             GR2_FN_FILE_READ(fd, buf, h.len) == (int)h.len && GR2_FN_FILE_READ(fd, &extra, 1) == 0 &&
             record_crc(h.gen, h.len, buf) == h.crc;
    GR2_FN_FILE_CLOSE(fd);
    if (!ok) return -1;
    *gen = h.gen;
    return (int)h.len;
}

int store_load(store_t *s, void *buf, uint32_t max) {
    uint32_t g0 = 0, g1 = 0;
    int n0 = read_one(s, 0, buf, max, &g0);
    int n1 = read_one(s, 1, buf, max, &g1);   /* buf now holds file 1 if it is valid */
    s->cur = -1;
    if (n1 >= 0 && (n0 < 0 || (int32_t)(g1 - g0) > 0)) {
        s->cur = 1;
        s->gen = g1;
        return n1;
    }
    if (n0 < 0) return -1;
    s->cur = 0;
    s->gen = g0;
    return n1 >= 0 ? read_one(s, 0, buf, max, &g0) : n0;
}

int store_save(store_t *s, const void *data, uint32_t len) {
    int i = s->cur == 0 ? 1 : 0;
    hdr_t h = { s->magic, s->cur < 0 ? 1 : s->gen + 1, len, 0 };
    h.crc = record_crc(h.gen, len, data);
    if (GR2_FN_FILE_MKDIR(DIR_PATH) < 0) return -1;
    int fd = GR2_FN_FILE_OPEN(s->path[i], FILE_WRITE);
    if (fd == 0) return -1;
    int ok = GR2_FN_FILE_WRITE(fd, &h, sizeof(h)) == (int)sizeof(h) &&
             GR2_FN_FILE_WRITE(fd, data, len) == (int)len;
    GR2_FN_FILE_CLOSE(fd);
    if (!ok) return -1;
    s->cur = (int8_t)i;
    s->gen = h.gen;
    return 0;
}
