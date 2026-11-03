#ifndef LOOK_H
#define LOOK_H
#include <stdint.h>

/* A recipe's colour look, applied by the CFW in IQ RAM (src/look.c, finding 20).
 * All stages are relative to the camera's own Standard rendering, so recipe
 * files carry no Ricoh data:
 *   matrix  camera colour matrix (linear RGB) = matrix/128 x stock
 *   hue     per hue-sector 3x3 (linear, 12 sectors) = hue[k]/128 x stock table k
 *   tone    per channel R, G, B: film code at Standard tone-curve code 8*i
 *           (i = 0..32, 256 = top), applied to the stock neutral/soft/hard
 *           curves so the contrast slider keeps working
 *   post    gamma-domain 3x3 RGB matrix /128, folded into the stock
 *           RGB->YCbCr (saturation) records so the saturation slider keeps working
 *   halo    film halation, not an IQ stage: the CFW adds it to the final still in
 *           the effect step (src/halation.c); spektrafilm's model: strength x1000
 *           per channel, the first-bounce sigma in um on 35 mm film and the
 *           highlight boost (boost_ev, boost_range, protect_ev) */
#define LOOK_KNOTS   33
#define LOOK_SECTORS 12

#define LOOK_MATRIX 0x01
#define LOOK_TONE   0x02
#define LOOK_POST   0x04
#define LOOK_HUE    0x08
#define LOOK_HALO   0x10

#define LOOK_HALO_MAX     1000           /* strength x1000 per channel */
#define LOOK_HALO_UM_MIN  20             /* first-bounce sigma, um */
#define LOOK_HALO_UM_MAX  130            /* src/halation.c: the third bounce still fits its kernel (RMAX) */
#define LOOK_HALO_UM_DEF  65             /* spektrafilm's default halation_first_sigma_um */
#define LOOK_BOOST_EV_MAX 60             /* boost_ev x10: the boosted white fits src/halation.c's 16-bit sources */
#define LOOK_BOOST_RANGE_DEF 30          /* boost_range x100: spektrafilm's default */
#define LOOK_BOOST_PROTECT_DEF 20        /* protect_ev x10 above mid grey (the camera's white is ~3.3 EV above) */
#define LOOK_BOOST_PROTECT_MAX 40

typedef struct {
    uint8_t flags;                       /* LOOK_* present */
    uint8_t halo_um;                     /* LOOK_HALO: first-bounce sigma, um */
    uint16_t halo[3];                    /* LOOK_HALO: strength x1000, R G B (not all 0) */
    uint8_t boost[3];                    /* LOOK_HALO: boost_ev x10 (0: none), boost_range x100, protect_ev x10 */
    int16_t matrix[9];
    int16_t post[9];
    int16_t hue[LOOK_SECTORS][9];
    uint8_t tone[3][LOOK_KNOTS];
} look_t;

/* IQ mode (k5) of a colour mode (prop 0x2D): Standard 0, Vivid 1, Setting1 2,
 * Setting2 3; -1 for modes the CFW does not change. */
int look_mode(uint32_t colour_mode);

/* Show look l in IQ mode k5 (0..3), or the stock look everywhere (l == 0 or
 * k5 < 0). Live view follows within a frame. Returns 0, or -1 if the IQ data
 * is not available (logged once). CFW task, or the loader's boot-task hooks
 * once look_iq_ready(). */
int look_apply(const look_t *l, int k5);

/* The ISP holds the stock IQ.bin (no log, no state change): the earliest a
 * look can be applied during boot. */
int look_iq_ready(void);

/* Value of a tone curve at Standard code x (0..255). */
int look_tone_at(const uint8_t *knots, int x);

/* How IQ mode k5 (0..3) renders a still right now, read from that mode's IQ lists as the ISP reads them: the R, G,
 * B tone curves (index = 10-bit linear RGB after the matrix, value = 8-bit code) blended for the stored contrast
 * `con` (1..9) as the firmware blends them, and the RGB'->YCbCr record (x256, rows Y Cb Cr) of the stored
 * saturation `sat` (1..9) and colour space `adobe`. 0, or -1 if the IQ data is not installed. CFW task. */
int look_still_model(int k5, uint32_t con, uint32_t sat, uint32_t adobe, uint8_t tone[3][1024], int16_t ycc[9]);

#endif /* LOOK_H */
