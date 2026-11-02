#ifndef STORE_H
#define STORE_H
#include <stdint.h>

/* Small records on the internal D: drive, two generations each (finding 34 §7):
 * every save rewrites only the older (or invalid) file, so a power loss during
 * a save can cost at most that save. File: {magic, generation, length, CRC-32
 * over generation, length and payload} + payload. CFW task, or the loader's
 * first hook (load only). */
typedef struct {
    const char *path[2];
    uint32_t magic;
    uint32_t gen;      /* generation of the newest valid file */
    int8_t cur;        /* index of that file, -1 none */
} store_t;

/* Newest valid payload into buf (at most max bytes): its length, or -1 if
 * neither file is valid. buf may be overwritten even when -1 is returned. */
int store_load(store_t *s, void *buf, uint32_t max);
/* Write payload into the older file: 0, or -1 if the write failed (the
 * newest valid file is then unchanged). */
int store_save(store_t *s, const void *data, uint32_t len);

#endif /* STORE_H */
