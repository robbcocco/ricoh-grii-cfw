#ifndef LOOK_H
#define LOOK_H
#include <stdint.h>

/* A recipe's colour look, applied by TTY in IQ RAM (src/look.c, finding 20).
 * All stages are relative to the camera's own Standard rendering, so recipe
 * files carry no Ricoh data:
 *   matrix  camera colour matrix (linear RGB) = matrix/128 x stock
 *   hue     per hue-sector 3x3 (linear, 12 sectors) = hue[k]/128 x stock table k
 *   tone    per channel R, G, B: film code at Standard tone-curve code 8*i
 *           (i = 0..32, 256 = top), applied to the stock neutral/soft/hard
 *           curves so the contrast slider keeps working
 *   post    gamma-domain 3x3 RGB matrix /128, folded into the stock
 *           RGB->YCbCr (saturation) records so the saturation slider keeps working */
#define LOOK_KNOTS   33
#define LOOK_SECTORS 12

#define LOOK_MATRIX 0x01
#define LOOK_TONE   0x02
#define LOOK_POST   0x04
#define LOOK_HUE    0x08

typedef struct {
    uint8_t flags;                       /* LOOK_* stages present */
    int16_t matrix[9];
    int16_t post[9];
    int16_t hue[LOOK_SECTORS][9];
    uint8_t tone[3][LOOK_KNOTS];
} look_t;

/* IQ mode (k5) of a colour mode (prop 0x2D): Standard 0, Vivid 1, Setting1 2,
 * Setting2 3; -1 for modes TTY does not change. */
int look_mode(uint32_t colour_mode);

/* Show look l in IQ mode k5 (0..3), or the stock look everywhere (l == 0 or
 * k5 < 0). Live view follows within a frame. Returns 0, or -1 if the IQ data
 * is not available (logged once). TTY task, or the loader's boot-task hooks
 * once look_iq_ready(). */
int look_apply(const look_t *l, int k5);

/* The ISP holds the stock IQ.bin (no log, no state change): the earliest a
 * look can be applied during boot. */
int look_iq_ready(void);

/* Value of a tone curve at Standard code x (0..255). */
int look_tone_at(const uint8_t *knots, int x);

#endif /* LOOK_H */
