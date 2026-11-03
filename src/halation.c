/* Film halation in final stills (finding 37 §1, §4.1; M17). All variants.
 *
 * Model (spektrafilm `apply_halation_um`, its defaults): exposure E2 = (E + a * sum_k w_k G(sigma sqrt k) * E) / (1 + a)
 * per channel, 3 bounces, w = 4/7, 2/7, 1/7. The recipe look is fitted to spektrafilm with halation off, so the
 * pass adds only the spatial part, a' (G * S - k A) with a' = a / (1 + a), and only where it is positive: the
 * scattered light of the highlights less the light already there, spektrafilm's renormalisation (inside an even
 * area the light a pixel scatters and receives cancel; k = 1.4, a margin: a faint ring on a lit wall is no glow).
 * The source is the highlights only: S = E w(E) boost(E) per channel (spektrafilm boosts each channel), w ramping
 * from 0 at E_LO to 1 at E_HI (2 and 3 EV above mid grey in the tone-curve domain); the boost fades smoothly with
 * the bright share of the 17 x 17 cells around (a lit wall or the sky is not a lamp; at full share its source is a
 * quarter of its light). The hold A is a smoothed local level, never a pixel's own: of the 3 x 3 cells' max(mean E,
 * mean S), their mean, at most twice their least (an even lit area holds its halo, a lamp's ring next to dark ground
 * does not), interpolated between cell centres; so a pixel next to a lamp gets the halo its darker neighbour gets.
 * It tapers only with the pixel's headroom (x H / (H + x), never past the top; none at the top of the brightest
 * channel: a clipped light keeps its colour). A pixel darker than its cells' level L under a faint glow takes L's
 * code step, back to its own as the glow grows to 2 L (a lit area's dark texture keeps its contrast, a lamp's ring
 * is each pixel's own). Green's code step
 * is at most red's and blue's at most green's (no olive or green fringe), a 4 x 4 Bayer rounding avoids contour
 * bands, and no channel of any pixel ever decodes darker (per-pixel luma headroom, a rounding fix-up).
 * Frames without highlights are left byte-identical.
 *
 * Where: the still effect step's call at GR2_FX_SITE_A, retargeted (verify-first, one halfword) to the stage-1
 * effect veneer only while the shown look has halation. The hook runs the stock step, then the pass if: the frame's
 * colour mode (ctx+0x408) is the one the look is shown in, ctx+0x357 = 0 (no stock effect routing), not the DNG's
 * own still (ctx+0x008 = 7: its embedded previews) or RAW development (ctx+0x1E0, the RAW handle, set), not an
 * interval composite or multiple exposure (SHOOTING_CONTEXT 1 / 3: stock disables its spatial effects there,
 * finding 18 §1), the tables are complete, the frame geometry is what the stock step uses, and the still path's
 * scratch buffer SPRWORK is free (stock allocator, polled: the stock step has released it, finding 37 §3.5).
 * Site B (FUN_a02AEECC) is RAW development's preview and is never patched.
 *
 * Domain: the developed frame is YCbCr 4:2:2 (full range) = C (T(lin)) with T the mode's tone curves (10-bit linear
 * index after the colour matrix -> 8-bit code, contrast blended) and C its RGB'->YCbCr record; both are read from the
 * IQ lists (src/look.c) by the CFW task, which builds every table here: C^-1 (Q12), per code the linear value (centre
 * of the code's interval), the source and the least halo that can change the code.
 *
 * Pass, streamed over a grid of 8 x 8 px cells (one 8-px word group wide), one cell row per step (scratch: SPRWORK,
 * cached address, ~0.1 MB used; every word is written before it is read):
 *   1. source of cell row i: the first pixel of every chroma pair in every 2nd row (4 x 4 samples, word reads):
 *      per channel the mean source (Q4) and the least code seen; a bound on Y and |Cb|, |Cr| rejects samples that
 *      cannot be a source without decoding them;
 *   2. blur of row b = i - rmax: per channel and bounce a separable binomial kernel whose variance plus that of the
 *      sampling and of the bilinear upsampling is the bounce's (sigma_um * 4928 / 35000 px: spektrafilm's 35 mm
 *      frame), the bounce weights folded into the vertical taps; rows without a source in reach are skipped;
 *      result a' blur(S);
 *   3. add back the full-resolution rows between the centres of rows b - 1 and b, in 8-px groups (word reads and
 *      writes, write-through): the halo bilinearly from the grid, h = a' blur(S) - a' S(pixel); where h >= 1/2
 *      linear unit: code' = T(T^-1(code) + h) per channel, the code deltas through C into Y (per pixel) and Cb/Cr
 *      (pair mean), clipped. A 2 x 2 cell block is skipped when no code in it can change (halo bound below the least
 *      step of its codes).
 * Nothing outside [plane, plane + w) of rows 0..h-1 is written; a frame or buffer that is not as expected is left
 * untouched. The hook runs in the still-processing task: no file I/O, no logging, small stack; the CFW task logs
 * its record. */
#include <stdint.h>
#include "boot.h"
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"
#include "halation.h"
#include "look.h"

#define CELL        8                  /* grid cell, px: one 8-px group of 2 Y words, 1 Cb and 1 Cr word */
#define NB          3                  /* bounces (spektrafilm halation_n_bounces), decay 0.5 */
_Static_assert(NB == 3, "blur_row() runs the three bounces' vertical taps together");
#define RMAX        24                 /* kernel radius, cells (LOOK_HALO_UM_MAX fits) */
#define MAX_W       624                /* cells per row: frames up to 4992 px wide */
#define CW          ((MAX_W + 31) / 32)   /* words of a cell mask (bit x & 31 of word x / 32: cell x) */
#define NBOX        4                  /* source pre-test boxes (src/halation_arm.S hx_scan4 tests 4) */
#define MAX_H       432                /* cell rows: up to 3456 px high */
#define E_LO        400                /* source ramp, linear units (1023 = top of the tone curve) */
#define E_HI        800
#define E_BIG       560                /* a cell whose samples are all above this is part of a bright area (soft_map) */
#define E_MID       100                /* mid grey, linear units: E_LO is 2 EV above, the top 3.35 EV */
#define BIG_S       8                  /* bright share over 2 BIG_S + 1 cells (136 px) around a cell: no boost */
#define BIG_N       (2 * BIG_S + 2)    /* soft_map()'s ring rows */
#define BW_W(W)     (((W) + 1) >> 1)   /* the share map's row: every 2nd cell */
#define F_LO        960                /* a pixel's brightest channel above this takes less of the halo, at the top none */
#define NY1_DEC     30000              /* more decoded groups in the estimate: one sample row per cell, not two */
#define H_MIN       8                  /* 1/2 linear unit, Q4: the least halo that is added */
/* the own light's weight, x a' (Q12): 1.4, a margin so only a halo well above the pixel's light shows (a faint
 * ring on a lit wall's shoulder is no visible glow, only banding and work) */
#define OWN_K(a)    ((a) * 45u >> 5)
#define H_MAX       32767              /* largest halo kept, Q4 (2x the top: any more is the top code too) */
/* Cost budget (over_budget()), us on the camera. The H2 shots' pass times [H] (finding 37: 277, 316, 779, 554 ms)
 * against the emulator's instruction counts of the same pass give the camera's rate: 12.5 ns per instruction, less
 * 34 ms. The estimate's terms per stage, fitted to the emulator's counts of this pass on the 13 H2 photos, three
 * indoor photos and a synthetic dense night street (tests/emu/halation_preview.py): the H2 photos within -10 ..
 * +23 %, the night street -1 %, the indoor ones up to 2.4x over (cells near a lamp on dark ground that show no halo).
 * A frame over the budget is left without halation (status 6). */
#define HX_BUDGET   3000000u
#define EST_FIX     240000u            /* the pre-test scans, the estimate's tables and maps, bookkeeping */
#define EST_DEC     2u                 /* a group of the estimate's sample row passing the pre-test (decoding) */
#define EST_ROW     4u                 /* the same group in each source sample row (2, 1 above NY1_DEC) */
#define EST_CELL    22u                /* a cell within rmax of a source (the blur, the add-back's tests) */
#define EST_BOOST   50u                /* a cell off the sources within rmax of a lamp (the add-back's halo) */
#define STAGE       __attribute__((noinline, noclone)) static   /* one function per stage: profiles split by stage */
#define NO_STEP     0xFFFFu
/* Variance (px^2, Q8) the grid adds: sampling (2 samples 4 px apart: 16 * 3 / 12) + bilinear upsampling (8^2 / 6). */
#define V_GRID_Q8   3755
/* (4928 px / 35000 um)^2, Q8 x 1000: spektrafilm's 35 mm frame on the GR II's 4928-px width. */
#define PX2_PER_UM2 5075

#define DRAM(a)     (((a) >> 29) == 5u)   /* 0xA0000000-0xBFFFFFFF: cached DRAM with an uncached alias */
static uint32_t cached(uint32_t a) { return (a >> 29) == 1u ? (a & 0x1FFFFFFFu) | 0xA0000000u : a; }
#define CTX32(o)    (*(const volatile uint32_t *)(ctx + (o)))
#define CTX8(o)     (*(const volatile uint8_t *)(ctx + (o)))

static const char FILE_TAG[] = "CFW halation";
#define BARRIER()   __asm__ volatile("" ::: "memory")   /* the hook (another task) sees the tables in order */

/* ---- tables (CFW task writes, the hook copies) ------------------------------------------------- */

typedef struct {
    uint32_t cm;                       /* colour mode the look is shown in (ctx+0x408) */
    int32_t m[9];                      /* YCbCr -> RGB', Q12, rows R G B: R' = m0 Y + m1 (Cb-128) + m2 (Cr-128) */
    int32_t c[9];                      /* RGB' -> YCbCr x256, rows Y Cb Cr (the mode's record) */
    uint32_t bk[NBOX][4];              /* source pre-test boxes (ky, kc, yhi, 0): see build() */
    uint32_t all;                      /* no box rejects anything: every group is decoded */
    uint32_t hdec;                     /* wide kernels: the horizontal pass on even cells, odd ones cubic between */
    uint32_t bg, rbg;                  /* blue's halo from green's: a'_B <= a'_G / 4; a'_B / a'_G, Q12 */
    uint32_t recip[17];                /* 32768 / n samples */
    uint32_t sh;                       /* sources are kept in units of 2^sh / 16 (boosted whites fit 16 bits) */
    uint32_t a[3];                     /* a' = a / (1 + a), Q12 */
    uint32_t rad[NB], rmax;            /* kernel radius per bounce, largest */
    int32_t kv[NB][RMAX + 1];          /* vertical taps, bounce weight folded in, Q14 */
    int32_t kh[NB][RMAX + 1];          /* horizontal taps, Q14 */
    uint16_t tinv[3][256];             /* code -> linear, Q4 (centre of the code's interval) */
    uint16_t gain[1024];               /* linear E of a channel -> w(E) boost(E) / E, Q9 */
    uint16_t gunb[1024];               /* a large bright area's gain: w(E) / 4, Q9 (no boost, a faint glow) */
    uint16_t fade[1024];               /* linear E of a pixel's brightest channel -> share of x it takes, Q8 */
    uint16_t rec[1024];                /* 65535 / n (hx_group: ratios to a level, the headroom, the luma room) */
    uint16_t step[3][256];             /* least halo (Q4) that changes a code >= k, NO_STEP: none */
    uint32_t ts[3][256];               /* tinv << 16 (src/halation_arm.S) */
    uint8_t t[3][4096];                /* tone curves (made monotone), index 1024.. = the top: no clamp */
    uint8_t top[3];                    /* each channel's top code */
} hp_t;

static hp_t g_p;
uint32_t g_hx_budget = HX_BUDGET;      /* us (tests/emu/halation_preview.py --no-budget raises it) */
static volatile uint32_t g_seq;        /* odd while g_p is written */
static volatile uint32_t g_valid;      /* g_p describes the shown look */
static uint32_t g_key[8];              /* inputs g_p was built from */
static uint32_t g_veneer;
static volatile uint32_t *g_target;
static int g_on, g_refused;

/* ---- shot record (the hook writes, the CFW task logs) ------------------------------------------ */

enum {
    HX_OK, HX_TABLES, HX_MODE, HX_RAW, HX_CONTEXT,                  /* normal: applied / not for this frame */
    HX_NONE,                                                        /* no highlight bright enough: nothing added */
    HX_BUSY,                                                        /* too many highlights for the budget */
    HX_NO_BUFFER, HX_NO_TABLE, HX_NO_CHROMA, HX_BAD_SIZE, HX_OUTSIDE, HX_ALIGN, HX_NO_SCRATCH, HX_SCRATCH_SIZE,
};
#define HX_FIRST_ERROR HX_NO_BUFFER

typedef struct {                       /* all words: the CFW task copies it word by word */
    uint32_t seq;                      /* odd while the hook writes */
    uint32_t shot, site, ctx;
    uint32_t mode;                     /* ctx +0x408 | +0x357 << 8 | +0x008 << 16 */
    uint32_t raw, wh, status;
    uint32_t y, scratch;               /* Y plane, scratch (cached) */
    uint32_t t_stock, t_pass;          /* ms */
    uint32_t cells, rows, groups, words;   /* source cells, rows with a halo, 8-px groups done, words written */
    uint32_t dec, nh;                  /* estimate: groups decoded, cells with a halo (0 if it stopped early) */
    uint32_t src;                      /* estimate: source cells in its sample rows (so far, if it stopped early) */
    uint32_t nb;                       /* estimate: cells near a boosted source (0 if it stopped before) */
} hx_rec_t;

static volatile hx_rec_t g_rec;
static volatile uint32_t g_shots;
static uint32_t g_logged_seq, g_logged_shot;

/* ---- the CFW task side ------------------------------------------------------------------------- */

void halation_init(const cfw_boot_t *boot) {
    g_veneer = boot->veneer_fx;
    g_target = boot->fx_target;
}

int halation_installed(void) {
    return g_on;
}

static uint32_t bl_words(uint32_t site) {
    return (uint32_t)*(const volatile uint16_t *)site << 16 | *(const volatile uint16_t *)(site + 2);
}

static void bl_set(int on) {
    if (on == g_on) return;
    if (on) {
        if (g_refused) return;
        if (!g_veneer || !g_target || !cfw_bl_calls(GR2_FX_SITE_A, GR2_FX_STEP)) {
            ERR_HEX("halation: effect-step call not stock, halation off", bl_words(GR2_FX_SITE_A));
            g_refused = 1;
            return;
        }
        *g_target = (uint32_t)halation_hook;   /* before the BL can reach the veneer */
        if (cfw_bl_retarget(GR2_FX_SITE_A, GR2_FX_STEP, g_veneer) != 0) {
            g_refused = 1;
            return;
        }
        g_on = 1;
        DBG("halation: on");
    } else {
        /* g_valid is already 0: a hook still running, or reached through a stale I-cache line, passes through. */
        if (cfw_bl_calls(GR2_FX_SITE_A, g_veneer)) (void)cfw_bl_retarget(GR2_FX_SITE_A, g_veneer, GR2_FX_STEP);
        g_on = 0;
        DBG("halation: off");
    }
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* a * b / d for a, b < 2^31, d > 0 (64-bit product, shift-subtract division: no libgcc). */
static int32_t muldiv(int32_t a, int32_t b, int32_t d) {
    int neg = (a < 0) ^ (b < 0) ^ (d < 0);
    uint32_t ua = (uint32_t)iabs(a), ub = (uint32_t)iabs(b), ud = (uint32_t)iabs(d);
    uint32_t al = ua & 0xFFFF, ah = ua >> 16, bl = ub & 0xFFFF, bh = ub >> 16;
    uint32_t lo = al * bl, mid1 = ah * bl, mid2 = al * bh, hi = ah * bh;
    uint32_t t = lo + (mid1 << 16);
    hi += (mid1 >> 16) + (t < lo);
    lo = t;
    t = lo + (mid2 << 16);
    hi += (mid2 >> 16) + (t < lo);
    lo = t;
    hi += ud >> 1 > ~lo ? 1 : 0;       /* round: + d/2 */
    lo += ud >> 1;
    uint32_t q = 0, r = hi;
    for (int i = 31; i >= 0; i--) {
        uint32_t top = r >> 31;
        r = (r << 1) | ((lo >> i) & 1);
        q <<= 1;
        if (top || r >= ud) { r -= ud; q |= 1; }
    }
    return neg ? -(int32_t)q : (int32_t)q;
}

/* m = (C / 256)^-1 in Q12; -1 if C is singular. */
static int invert(const int16_t *c, int32_t *m) {
    int32_t a[9];
    for (int i = 0; i < 9; i++) a[i] = c[i];
    int32_t adj[9] = {
        a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4],
        a[5] * a[6] - a[3] * a[8], a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
        a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7], a[0] * a[4] - a[1] * a[3],
    };
    int32_t det = a[0] * adj[0] + a[1] * adj[3] + a[2] * adj[6];
    if (iabs(det) < 0x10000) return -1;
    for (int i = 0; i < 9; i++) m[i] = muldiv(adj[i], 1 << 20, det);   /* 256 * 4096 * adj / det */
    return 0;
}

static uint32_t isqrt(uint32_t x) {
    uint32_t r = 0;
    for (uint32_t b = 1u << 30; b; b >>= 2) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
    }
    return r;
}

/* Symmetric discrete kernel of variance v (cells^2, Q8): n stages of [a, 1 - 2a, a], a = v / 2n, Q14. n reaches 4
 * sigma (a boosted source's tail must not end in a visible step) and keeps a <= 1/3 (non-negative, unimodal); a
 * variance past RMAX stages is clipped. */
static uint32_t kernel(uint32_t v_q8, int32_t *k) {
    uint32_t n = (3 * v_q8 + 511) >> 9, n4 = (4 * isqrt(v_q8 << 8) + 255) >> 8;   /* 1.5 v; 4 sqrt(v) */
    if (n4 > n) n = n4;
    if (n > RMAX) {
        n = RMAX;
        if (v_q8 > RMAX * 512 / 3) v_q8 = RMAX * 512 / 3;
    }
    int32_t t[2 * RMAX + 3] = { 0 }, u[2 * RMAX + 3];
    t[RMAX + 1] = 16384;
    int32_t a = n ? (int32_t)udiv32(v_q8 * 32, n) : 0;
    for (uint32_t s = 0; s < n; s++) {
        for (int i = 1; i <= 2 * RMAX + 1; i++) u[i] = ((16384 - 2 * a) * t[i] + a * (t[i - 1] + t[i + 1]) + 8192) >> 14;
        for (int i = 1; i <= 2 * RMAX + 1; i++) t[i] = u[i];
    }
    int32_t sum = 0;
    for (uint32_t i = 0; i <= RMAX; i++) {
        k[i] = i <= n ? t[RMAX + 1 + i] : 0;
        sum += i ? 2 * k[i] : k[i];
    }
    k[0] += 16384 - sum;
    return n;
}

/* 2^x, x in Q16 (x <= 14 << 16), Q16; 0 below 2^-16. Taylor to the 5th order on the fraction (error < 2e-4). */
static uint32_t exp2_q16(int32_t x) {
    if (x < -(16 << 16)) return 0;
    int32_t n = x >> 16;
    uint32_t f = (uint32_t)x & 0xFFFFu, q = 87;         /* ln2^k / k!, Q16: k = 5 .. 1 */
    q = 630 + ((q * f) >> 16);
    q = 3638 + ((q * f) >> 16);
    q = 15743 + ((q * f) >> 16);
    q = 45426 + ((q * f) >> 16);
    q = 65536 + ((q * f) >> 16);
    return n >= 0 ? q << n : q >> -n;
}

/* e^-t, t in Q16 (>= 0), Q16. */
static uint32_t expm_q16(uint32_t t) {
    return t >= (12u << 16) ? 0 : exp2_q16(-(int32_t)((t * 1477u) >> 10));   /* log2 e = 1477 / 1024 */
}

/* spektrafilm's highlight boost (utils/numba_boost_hightlights.py) on linear E (0..1023, the camera's white as
 * np.max(x)): E for E <= x0 = mid grey x 2^protect_ev, else E + 1023 k (e^(a dx) - a dx - 1), dx = (E - x0) / 1023,
 * a = 28^(1 - boost_range), k = (2^boost_ev - 1) / (e^(a L) - a L - 1), L = 1 - x0 / 1023: the white rises by
 * boost_ev. Here as (u - (a dx + 1) e^-aL) / (1 - (a L + 1) e^-aL), u = e^-a(L - dx), so nothing overflows. bst:
 * the look's boost (x10, x100, x10). */
static uint32_t boost(uint32_t e, const uint8_t *bst) {
    if (!bst[0]) return e;
    uint32_t x0 = (E_MID * exp2_q16((int32_t)udiv32((uint32_t)bst[2] << 16, 10)) + 32768) >> 16;
    if (e <= x0 || x0 >= 1023) return e;
    uint32_t a8 = exp2_q16((int32_t)udiv32((100u - bst[1]) * 315049u, 100)) >> 8;   /* 28^(1 - r), Q8 */
    uint32_t L = udiv32((1023 - x0) << 16, 1023), dx = udiv32((e - x0) << 16, 1023);   /* Q16 */
    uint32_t aL = (a8 * L) >> 8, adx = (a8 * dx) >> 8;                            /* Q16 */
    uint32_t eL = expm_q16(aL), u = expm_q16(aL - adx);
    uint32_t num = u - (((adx >> 8) + 256) * eL >> 8), den = 65536 - (((aL >> 8) + 256) * eL >> 8);
    if ((int32_t)num <= 0 || (int32_t)den <= 0) return e;
    uint32_t f = udiv32(num << 15, den) << 1;                                     /* Q16, 0..1 */
    uint32_t gain = 1023 * ((exp2_q16((int32_t)udiv32((uint32_t)bst[0] << 16, 10)) - 65536) >> 6);   /* Q10 */
    return e + (uint32_t)(((uint64_t)gain * f) >> 26);
}

static int build(const look_t *l, int k5, uint32_t cm, uint32_t con, uint32_t sat, uint32_t adobe) {
    hp_t *p = &g_p;
    int16_t ycc[9];
    static uint8_t tone[3][1024];
    if (look_still_model(k5, con, sat, adobe, tone, ycc) != 0) return -1;
    for (int i = 0; i < 9; i++) p->c[i] = ycc[i];
    if (invert(ycc, p->m) != 0) return -1;
    p->cm = cm;
    p->sh = 0;                                          /* 16 x the boosted white in 16 bits */
    while ((boost(1023, l->boost) << 4 >> p->sh) > 65535) p->sh++;
    /* Per channel, its E selects the ramp weight w (E_LO .. E_HI) and the boost (spektrafilm boosts each channel):
     * S = E w boost / E (the boost restores the clipped light). */
    for (uint32_t e = 0; e < 1024; e++) {
        uint32_t b = boost(e, l->boost), g = e <= E_LO ? 0 : udiv32(b * 512 + e / 2, e);
        p->gain[e] = (uint16_t)(e >= E_HI ? g : udiv32(g * (e - E_LO) + 200, E_HI - E_LO));
        p->gunb[e] = (uint16_t)(e <= E_LO ? 0 : e >= E_HI ? 128 : udiv32(128 * (e - E_LO) + 200, E_HI - E_LO));
        p->fade[e] = (uint16_t)(e <= F_LO ? 256 : udiv32(256 * (1023 - e) + (1023 - F_LO) / 2, 1023 - F_LO));
    }
    p->rec[0] = 0;
    for (uint32_t n = 1; n < 1024; n++) p->rec[n] = (uint16_t)udiv32(65535, n);
    uint32_t code0 = 256;
    for (int c = 0; c < 3; c++) {
        uint8_t *t = p->t[c];
        for (int i = 0; i < 4096; i++) {                 /* monotone, extended */
            uint8_t v = tone[c][i < 1024 ? i : 1023];
            t[i] = i && v < t[i - 1] ? t[i - 1] : v;
        }
        p->top[c] = t[1023];
        uint32_t h = l->halo[c];
        p->a[c] = h ? udiv32(4096 * h + (1000 + h) / 2, 1000 + h) : 0;
        uint32_t lo = 0, thr[256];
        for (uint32_t k = 0; k < 256; k++) {
            while (lo < 1024 && t[lo] < k) lo++;
            uint32_t hi = lo;                               /* first index above k */
            while (hi < 1024 && t[hi] <= k) hi++;
            uint32_t centre = lo + hi ? 8 * (lo + hi - 1) : 0;   /* Q4 centre of [lo, hi); empty: the boundary */
            if (centre > 16 * 1023) centre = 16 * 1023;
            p->tinv[c][k] = (uint16_t)centre;
            thr[k] = hi >= 1024 ? NO_STEP : 8 * (hi - lo) > H_MIN ? 8 * (hi - lo) : H_MIN;
            uint32_t e = (centre + 8) >> 4;
            p->ts[c][k] = centre << 16;
            if (e > E_LO && k < code0) code0 = k;
        }
        uint32_t mn = NO_STEP;
        for (int k = 255; k >= 0; k--) {
            if (thr[k] < mn) mn = thr[k];
            p->step[c][k] = (uint16_t)mn;
        }
    }
    /* Pre-test boxes: Y <= ny and every chroma byte in 128 - D .. 127 + D (D a power of two: the folded chroma has
     * no bit >= D). A channel's code there is at most (m_c0 Y + (|m_c1| + |m_c2|) D + 2048) >> 12; below code0 (the
     * least source code) no sample is a source. A group is decoded only if no box holds all its bytes: from a tight
     * chroma box (neutral greys up to a high Y) to a wide one (saturated colours, darker). ny falls with D: the boxes
     * that reject something come first. */
    static const uint8_t DS[NBOX] = { 8, 16, 32, 64 };
    int32_t ny[NBOX];
    for (int i = 0; i < NBOX; i++) {
        int32_t lim = 256;
        for (int c = 0; c < 3; c++) {
            int32_t m0 = p->m[3 * c];
            int32_t rest = 4096 * (int32_t)code0 - 2048 - (iabs(p->m[3 * c + 1]) + iabs(p->m[3 * c + 2])) * DS[i];
            int32_t y = m0 <= 0 ? (rest > 0 ? 256 : 0) : rest <= 0 ? 0 : (int32_t)udiv32((uint32_t)rest, (uint32_t)m0);
            if (y < lim) lim = y;                       /* Y < y keeps channel c below code0 (m0 Y < rest) */
        }
        ny[i] = code0 > 255 || lim >= 256 ? 255 : lim - 1;   /* reject if no Y byte > ny (Y < lim) */
    }
    p->all = ny[0] < 0;
    for (int i = 0; i < NBOX; i++) {
        int j = ny[i] < 0 ? 0 : i;                      /* a box that rejects nothing: box 0 again */
        uint32_t n = (uint32_t)ny[j];
        p->bk[i][0] = 0x01010101u * (n >= 128 ? 255u - n : 127u - n);
        p->bk[i][1] = code0 > 255 ? 0 : 0x01010101u * (0x7Fu & ~(DS[j] - 1u));
        p->bk[i][2] = n >= 128;
        p->bk[i][3] = 0;
    }
    DBG_HEX2("halation: pre-test (least source code, Y bounds of the boxes D 8 16 32 64)", code0,
             (uint32_t)(ny[0] & 0xFF) << 24 | (uint32_t)(ny[1] & 0xFF) << 16 | (uint32_t)(ny[2] & 0xFF) << 8 |
             (uint32_t)(ny[3] & 0xFF));
    p->recip[0] = 0;
    for (uint32_t n = 1; n <= 16; n++) p->recip[n] = udiv32(32768, n);
    uint32_t um = l->halo_um ? l->halo_um : LOOK_HALO_UM_DEF;
    int32_t wsum = 0;
    p->rmax = 0;
    p->hdec = udiv32(um * um * PX2_PER_UM2 + 500, 1000) >= 144 * 256;   /* first bounce sigma >= 12 px */
    p->bg = p->a[1] && 4 * p->a[2] <= p->a[1];
    p->rbg = p->bg ? udiv32(4096 * p->a[2] + p->a[1] / 2, p->a[1]) : 0;
    for (int b = 0; b < NB; b++) {
        uint32_t v = udiv32(um * um * (uint32_t)(b + 1) * PX2_PER_UM2 + 500, 1000);
        v = v > V_GRID_Q8 ? udiv32(v - V_GRID_Q8 + 32, CELL * CELL) : 0;
        p->rad[b] = kernel(v, p->kh[b]);
        if (p->rad[b] > p->rmax) p->rmax = p->rad[b];
        int32_t w = 4 >> b;                                 /* 4/7, 2/7, 1/7 */
        for (int i = 0; i <= RMAX; i++) {
            p->kv[b][i] = (int32_t)udiv32((uint32_t)p->kh[b][i] * (uint32_t)w + 3, 7);
            wsum += i ? 2 * p->kv[b][i] : p->kv[b][i];
        }
    }
    p->kv[0][0] += 16384 - wsum;
    return 0;
}

void halation_sync(const look_t *l, int cm, uint32_t gen) {
    int k5 = cm >= 0 ? look_mode((uint32_t)cm) : -1;
    int want = l && (l->flags & LOOK_HALO) && k5 >= 0;
    if (want) {
        uint32_t key[8] = {
            gen, (uint32_t)cm, l->halo[0] | (uint32_t)l->halo[1] << 16, l->halo[2] | (uint32_t)l->halo_um << 16,
            GR2_FN_PROP_GET_RAW(GR2_PROPID_COLOR_MODE_CON + (uint32_t)cm),
            GR2_FN_PROP_GET_RAW(GR2_PROPID_COLOR_MODE_SAT + (uint32_t)cm),
            GR2_FN_PROP_GET_RAW(GR2_PROPID_COLOR_SPACE),
            1u | (uint32_t)l->boost[0] << 8 | (uint32_t)l->boost[1] << 16 | (uint32_t)l->boost[2] << 24,
        };
        int same = 1;                                   /* g_key[7] bit 0: built (or tried) from g_key */
        for (int i = 0; i < 8; i++) same &= key[i] == g_key[i];
        if (!same) {
            g_seq++;
            g_valid = 0;
            BARRIER();
            int rc = build(l, k5, (uint32_t)cm, key[4], key[5], key[6]);
            BARRIER();
            g_valid = rc == 0;
            g_seq++;
            for (int i = 0; i < 8; i++) g_key[i] = key[i];   /* a failure is not retried until an input changes */
            if (rc != 0) ERR_HEX("halation: IQ data not available, no halation in IQ mode", (uint32_t)k5);
            else DBG_HEX2("halation: tables (IQ mode, contrast | saturation << 8)", (uint32_t)k5,
                          key[4] | key[5] << 8);
        }
    } else if (g_key[7]) {
        g_seq++;
        g_valid = 0;
        g_seq++;
        g_key[7] = 0;
    }
    bl_set(want && g_valid);
}

/* ---- the pass (still-processing task) ---------------------------------------------------------- */

typedef struct {
    uint32_t y, cb, cr, py, pc, w, h;  /* cached plane addresses, pitches, frame size */
} frame_t;

/* One cell row per step: the source of cell row i, then the blur of row b = i - rmax (its window is complete), then
 * the add-back of the full-resolution rows between the centres of rows b - 1 and b. The add-back writes only rows
 * above 8 i + 3 (rmax 0: rows the source of row i has read already), so every sample is read unmodified. */
#define RING (2 * RMAX + 2)            /* source rows kept: the blur window and the row before it */

typedef struct {                       /* src/halation_arm.S hx_group: K_* offsets */
    const uint8_t *t[3];               /* +0 */
    const uint32_t *ts[3];             /* +12 */
    int32_t m[9];                      /* +24 */
    int32_t c[9];                      /* +60 */
    int32_t h[12];                     /* +96: halo of the row per channel (R, G, B) and pair, Q4 */
    int32_t h2[12];                    /* +144: the same less the cell's own light (interpolated), Q4 */
    int32_t *hp;                       /* +192: the group's 2 x (12 accumulators (Q16), then their 12 row steps) */
    uint32_t a[3];                     /* +196: OWN_K(a'), Q12 */
    uint8_t dith[4];                   /* +208: the row's rounding per pair (Q4, 4 x 4 Bayer): no banding */
    const uint16_t *fade;              /* +212: hp_t fade */
    const uint16_t *rec;               /* +216: hp_t rec */
} hx_kern_t;
_Static_assert(sizeof(hx_kern_t) == 220, "hx_kern_t layout (src/halation_arm.S)");
uint32_t hx_group(hx_kern_t *k, uint32_t *y, uint32_t *cb, uint32_t *cr);   /* ARM */

typedef struct {                       /* src/halation_arm.S hx_src: S_* offsets */
    const uint32_t *y, *cb, *cr;       /* +0: the sample row's words */
    int32_t m[9];                      /* +12: C^-1, Q12 */
    const uint16_t *tinv[3];           /* +48 */
    const uint16_t *gain;              /* +60 */
    uint32_t top[3];                   /* +64: each channel's top code */
    uint32_t sh9;                      /* +76: 9 + sh */
    uint32_t fl;                       /* +80: channels with a source (1 R, 2 G, 4 B) */
    uint32_t *acc[3];                  /* +84: per group sums */
    uint8_t *npass, *nsat;             /* +96 */
    uint8_t *bmin[3];                  /* +104 */
    uint32_t *acc2[3];                 /* +116: per group own-light sums */
    const uint16_t *gunb;              /* +128: the unboosted gain */
    const uint8_t *bw;                 /* +132: the cell row's bright share q (0 .. 64, per 2 cells): the boost fades */
} hx_src_t;
_Static_assert(sizeof(hx_src_t) == 136, "hx_src_t layout (src/halation_arm.S)");
void hx_src(hx_src_t *k, uint32_t g, uint32_t e);   /* ARM */

typedef struct {
    hp_t p;                            /* the tables, copied */
    uint32_t W, H, ring;               /* cells (W = frame width / 8), rows held in the source ring */
    uint16_t *s[3];                    /* source ring: ring x W mean sources (Q4) per channel */
    uint16_t *so[3];                   /* own-light ring: ring x W mean max(E, source) (Q4) per channel */
    uint8_t *mc[3];                    /* ring x W: least code per channel seen in the cell (0: not all decoded) */
    uint32_t *tm;                      /* ring x CW: cells with a source */
    uint32_t *sm;                      /* ring x CW: cells whose every sample is at the top code of every channel */
    uint8_t *slot;                     /* H: ring slot of each cell row */
    uint16_t rl[RING], rr[RING];       /* cells a ring slot may hold non-zero (rl > rr: none) */
    uint16_t *b[3];                    /* 2 x W: a' blur(S) (Q4) of rows b - 1 and b, in slot b & 1 */
    int16_t *b2[3];                    /* 2 x W: the same less a' S of the cell (Q4, signed) */
    uint32_t bxl[2], bxr[2];           /* that row: first / last cell with a halo >= H_MIN (bxl > bxr: none) */
    uint32_t zm[2][CW];                /* cells of that slot that may be non-zero */
    uint16_t *glist;                   /* W: groups of the current band that can change */
    uint32_t *acc, *acc2;              /* 3 x W source / own-light sums of the cell row being sampled */
    uint8_t *bmin[3], *npass, *nsat;   /* W: least code per channel decoded, sample rows decoded, samples at the top */
    int32_t *vt, *ho;                  /* W: blur temporaries */
    uint32_t bslot[2 * RMAX + 1];      /* blur_row: ring slots and rows of its window */
    const uint16_t *brow[2 * RMAX + 1];
    int32_t *hp;                       /* 48 x W: hx_kern_t hp of each listed group */
    uint32_t *em;                      /* H x (W + 31) / 32: over_budget()'s source / halo cell bitmap */
    uint32_t *big;                     /* the same: cells whose samples are bright (E >= E_BIG) */
    uint8_t *bw;                       /* H/2 x W/2: the bright share around a cell, 0 .. 64 (soft_map()) */
    uint8_t *hring;                    /* BIG_N x W: soft_map()'s horizontal counts */
    uint16_t *cs;                      /* W: soft_map()'s column sums */
    uint8_t *span;                     /* W + 1: 2 x 2 cell blocks that can change (span j: cells j - 1, j) */
    uint32_t cells, rows, groups, words;
    uint32_t ny1;                      /* one sample row per cell (a frame with much to decode) */
    hx_kern_t kern;                    /* the add-back's tables and the group's halo */
    hx_src_t src;                      /* source_row()'s tables */
} scr_t;

static uint32_t align4(uint32_t a) { return (a + 3) & ~3u; }

static uint32_t layout(scr_t **out, uint32_t base, uint32_t size, const frame_t *f) {
    uint32_t W = f->w >> 3, H = (f->h + CELL - 1) >> 3, R = RING;
    if (W > MAX_W || H > MAX_H) return HX_BAD_SIZE;
    uint32_t a = base;
    scr_t *s = (scr_t *)a;
    a = align4(a + sizeof(scr_t));
    for (int c = 0; c < 3; c++) { s->s[c] = (uint16_t *)a; a = align4(a + 2 * R * W); }
    for (int c = 0; c < 3; c++) { s->so[c] = (uint16_t *)a; a = align4(a + 2 * R * W); }
    for (int c = 0; c < 3; c++) { s->mc[c] = (uint8_t *)a; a = align4(a + R * W); }
    s->tm = (uint32_t *)a; a += 4 * CW * R;
    s->sm = (uint32_t *)a; a += 4 * CW * R;
    s->slot = (uint8_t *)a; a = align4(a + H);
    for (int c = 0; c < 3; c++) { s->b[c] = (uint16_t *)a; a = align4(a + 4 * W); }
    for (int c = 0; c < 3; c++) { s->b2[c] = (int16_t *)a; a = align4(a + 4 * W); }
    s->acc = (uint32_t *)a; a += 12 * W;
    s->acc2 = (uint32_t *)a; a += 12 * W;
    for (int c = 0; c < 3; c++) { s->bmin[c] = (uint8_t *)a; a = align4(a + W); }
    s->npass = (uint8_t *)a; a = align4(a + W);
    s->nsat = (uint8_t *)a; a = align4(a + W);
    s->vt = (int32_t *)a; a += 4 * NB * W;
    s->ho = (int32_t *)a; a += 4 * W;
    s->hp = (int32_t *)a; a += 192 * W;
    s->em = (uint32_t *)a; a += 4 * H * ((W + 31) >> 5);
    s->big = (uint32_t *)a; a += 4 * H * ((W + 31) >> 5);
    s->bw = (uint8_t *)a; a = align4(a + ((H + 1) >> 1) * BW_W(W));
    s->hring = (uint8_t *)a; a = align4(a + BIG_N * W);
    s->cs = (uint16_t *)a; a = align4(a + 2 * W);
    s->span = (uint8_t *)a; a = align4(a + W + 1);
    s->glist = (uint16_t *)a; a = align4(a + 2 * W);
    if (a - base > size) return HX_SCRATCH_SIZE;
    s->W = W;
    s->H = H;
    s->ring = R;
    for (int o = 0; o < 2; o++) {
        s->bxl[o] = W;                                  /* empty */
        s->bxr[o] = 0;
        for (int w = 0; w < CW; w++) s->zm[o][w] = 0;
        for (int c = 0; c < 3; c++)
            for (uint32_t x = 0; x < W; x++) s->b[c][o * W + x] = 0, s->b2[c][o * W + x] = 0;
    }
    for (uint32_t k = 0; k < R; k++) {                  /* the ring: unknown */
        s->rl[k] = 0;
        s->rr[k] = (uint16_t)(W - 1);
    }
    for (uint32_t x = 0; x < NB * W; x++) s->vt[x] = 0;
    *out = s;
    return HX_OK;
}

/* `rows` rows of `width` bytes from `a`, `pitch` apart, lie inside [buf, buf + size) (values 16-bit bounded). */
static int inside(uint32_t a, uint32_t pitch, uint32_t width, uint32_t rows, uint32_t buf, uint32_t size) {
    if (a < buf || a - buf > size || !width || !rows) return 0;
    return (rows - 1) * pitch + width <= size - (a - buf);
}

static const volatile uint8_t *buf_type(uint32_t type) {
    const volatile uint8_t *t = (const volatile uint8_t *)(GR2_IMGBUF_TABLE + type * GR2_IMGBUF_ENTRY);
    return t[0] == type ? t : 0;
}

static uint32_t frame(uint32_t ctx, frame_t *f) {
    uint32_t h = CTX32(0x1F0);
    if (!h || (h & 3) || !DRAM(h) || *(const volatile uint32_t *)h != GR2_IMGBUF_MAGIC) return HX_NO_BUFFER;
    uint32_t stored = *(const volatile uint32_t *)(h + 4), buf = cached(stored);
    const volatile uint8_t *type = buf_type(GR2_IMGBUF_YCC);
    if (!type) return HX_NO_TABLE;
    uint32_t size = *(const volatile uint32_t *)(type + 4);
    if (!DRAM(buf) || !size || !DRAM(buf + size - 1) || buf + size < buf) return HX_NO_BUFFER;
    uint32_t y = stored, cb = 0, cr = 0;
    GR2_FN_YCC_PLANES(&y, &cb, &cr, ctx, -1, 3);
    if (!cb || !cr) return HX_NO_CHROMA;
    f->y = y = cached(y);
    f->cb = cb = cached(cb);
    f->cr = cr = cached(cr);
    uint32_t wh = CTX32(0x258), w = wh & 0xFFFFu, hh = wh >> 16;
    uint32_t py = CTX32(0x224), pc = CTX32(0x228), rows = CTX32(0x22C);
    if (w < 2 * CELL || !hh || (w & 7) || py > 0xFFFFu || pc > 0xFFFFu || rows > 0xFFFFu || w > py ||
        (w >> 1) > pc || hh > rows)
        return HX_BAD_SIZE;
    if ((y | cb | cr | py | pc) & 3) return HX_ALIGN;
    if (!inside(y, py, w, hh, buf, size) || !inside(cb, pc, w >> 1, hh, buf, size) ||
        !inside(cr, pc, w >> 1, hh, buf, size))
        return HX_OUTSIDE;
    f->py = py;
    f->pc = pc;
    f->w = w;
    f->h = hh;
    return HX_OK;
}

static int32_t clamp255(int32_t v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* Some byte of x is above n (k = 255 - n for n >= 128, else 127 - n; from build()): exact. */
#define M7F 0x7F7F7F7Fu
#define ANY_GT(x, k, hi) ((hi) ? (((x) & M7F) + (k)) & (x) : (((x) & M7F) + (k)) | (x))

/* Each chroma byte c to its distance from the neutral 128 (c - 128, or 127 - c below 128): c is within 128 +- D
 * (128 - D <= c <= 127 + D) exactly when the folded byte is below D. */
static uint32_t fold(uint32_t c) {
    uint32_t n = ~c & 0x80808080u;
    return c ^ (n - (n >> 7));
}

/* The 8-px group may hold a source: no box of build() holds all its bytes (folded chroma without a bit >= D, no Y
 * byte above ny). src/halation_arm.S hx_scan4 is the same test on blocks of 4 groups. */
static uint32_t hit(const uint32_t *yp, const uint32_t *bp, const uint32_t *rp, const hp_t *p) {
    uint32_t y0 = yp[0], y1 = yp[1], u = fold(bp[0]) | fold(rp[0]);
    for (int i = 0; i < NBOX; i++) {
        const uint32_t *k = p->bk[i];
        if (!(u & k[1]) && !((ANY_GT(y0, k[0], k[2]) | ANY_GT(y1, k[0], k[2])) & 0x80808080u)) return 0;
    }
    return 1;
}

uint32_t hx_scan4(const uint32_t *y, const uint32_t *cb, const uint32_t *cr, const uint32_t (*k)[4],
                  uint32_t n);   /* ARM */

/* Next group >= g of the row to decode, W if none: every group of a block of 4 (32 px) that may hold a source
 * (src/halation_arm.S hx_scan4, the same test as hit(); decoding a group that holds none is exact, only slower),
 * the last W mod 4 groups one by one. *end: the end of the block being decoded (W: every group). */
static uint32_t next_group(const uint32_t *yp, const uint32_t *bp, const uint32_t *rp, uint32_t g, uint32_t W,
                           const hp_t *p, uint32_t *end) {
    if (g < *end) return g;
    g += 4 * hx_scan4(yp + 2 * g, bp + g, rp + g, p->bk, (W - g) >> 2);
    if (g + 4 <= W) {
        *end = g + 4;
        return g;
    }
    for (; g < W; g++)
        if (hit(yp + 2 * g, bp + g, rp + g, p)) {
            *end = g + 1;
            return g;
        }
    return W;
}

/* 1. Cell row i: the mean source (Q4) and the least code per channel, from the first pixel of chroma pairs 1 and 3
 * of every group in rows 8i + 2 and 8i + 6 (2 x 2 samples 4 px apart; centroid 4 px into the cell, 1/2 px off its
 * centre); a cell row of at most 2 rows (the frame's last): row 8i. A block of 4 groups is decoded only if hx_scan4
 * says so: build() chose its boxes so that no pixel inside one reaches a source code in any channel. */
STAGE void source_row(scr_t *s, const frame_t *f, uint32_t i) {
    const hp_t *p = &s->p;
    uint32_t W = s->W, y0 = CELL * i, slot = s->slot[i];
    uint32_t *a0 = s->acc, *a1 = a0 + W, *a2 = a1 + W;
    uint32_t all = p->all;
    uint32_t gl = W, gr = 0;                            /* groups decoded; [gl, W) is initialised */
    hx_src_t *k = &s->src;
    k->bw = s->bw + (i >> 1) * BW_W(W);
    uint32_t rows = f->h - y0 < CELL ? f->h - y0 : CELL;
    for (uint32_t r = rows > 2 ? 2 : 0; r < rows; r += s->ny1 ? CELL : 4) {
        const uint32_t *yp = (const uint32_t *)(f->y + (y0 + r) * f->py);
        const uint32_t *bp = (const uint32_t *)(f->cb + (y0 + r) * f->pc);
        const uint32_t *rp = (const uint32_t *)(f->cr + (y0 + r) * f->pc);
        k->y = yp;
        k->cb = bp;
        k->cr = rp;
        uint32_t end = all ? W : 0;
        for (uint32_t g = next_group(yp, bp, rp, 0, W, p, &end); g < W; g = next_group(yp, bp, rp, g, W, p, &end)) {
            uint32_t e = end < W ? end : W;             /* the block (or the rest of the row) */
            if (g < gl) {                               /* initialise up to what is initialised already */
                for (uint32_t x = g; x < gl; x++) {
                    a0[x] = a1[x] = a2[x] = 0;
                    s->acc2[x] = s->acc2[W + x] = s->acc2[2 * W + x] = 0;
                    s->npass[x] = s->nsat[x] = 0;
                    s->bmin[0][x] = s->bmin[1][x] = s->bmin[2][x] = 255;
                }
                gl = g;
            }
            if (e - 1 > gr) gr = e - 1;
            hx_src(k, g, e);
            g = e;
        }
    }
    /* The ring row: zeros and mc 0 outside the decoded groups (only where the slot held something), inside the means
     * (a group not decoded in every sample row has mc 0). */
    uint32_t ny = rows > 6 && !s->ny1 ? 2 : 1, rc = p->recip[2 * ny], tb[CW] = { 0 }, sb[CW] = { 0 };
    uint16_t *d0 = s->s[0] + slot * W, *d1 = s->s[1] + slot * W, *d2 = s->s[2] + slot * W;
    uint8_t *mc0 = s->mc[0] + slot * W, *mc1 = s->mc[1] + slot * W, *mc2 = s->mc[2] + slot * W;
    for (uint32_t x = s->rl[slot], xe = s->rr[slot]; x <= xe; x++) {
        if (x >= gl && x <= gr) { x = gr; continue; }
        d0[x] = d1[x] = d2[x] = 0;
        for (int c = 0; c < 3; c++) s->so[c][slot * W + x] = 0;
        mc0[x] = mc1[x] = mc2[x] = 0;
    }
    s->rl[slot] = (uint16_t)gl;
    s->rr[slot] = (uint16_t)gr;
    for (uint32_t x = gl; x <= gr; x++) {
        uint32_t v0 = (a0[x] * rc + 16384) >> 15, v1 = (a1[x] * rc + 16384) >> 15, v2 = (a2[x] * rc + 16384) >> 15;
        d0[x] = (uint16_t)v0;
        d1[x] = (uint16_t)v1;
        d2[x] = (uint16_t)v2;
        for (int c = 0; c < 3; c++) s->so[c][slot * W + x] = (uint16_t)((s->acc2[c * W + x] * rc + 16384) >> 15);
        uint32_t all_ = s->npass[x] == ny;
        mc0[x] = all_ ? s->bmin[0][x] : 0;
        mc1[x] = all_ ? s->bmin[1][x] : 0;
        mc2[x] = all_ ? s->bmin[2][x] : 0;
        if (s->nsat[x] == 2 * ny) sb[x >> 5] |= 1u << (x & 31);
        if (v0 | v1 | v2) {
            s->cells++;
            tb[x >> 5] |= 1u << (x & 31);
        }
    }
    for (int w = 0; w < CW; w++) {
        s->tm[CW * slot + w] = tb[w];
        s->sm[CW * slot + w] = sb[w];
    }
}

/* x over the set bits of a cell mask, in order (word by word, empty words skipped) */
#define FOR_CELLS(mask, x)                                                                                   \
    for (uint32_t w_ = 0, b_; w_ < CW; w_++)                                                                 \
        for (b_ = (mask)[w_]; b_; b_ &= b_ - 1)                                                              \
            for (uint32_t x = 32 * w_ + 31 - (uint32_t)__builtin_clz(b_ & -b_), once_ = 1; once_; once_ = 0)

/* d |= d dilated by n cells (no cell past W) */
static void dilate(uint32_t *d, uint32_t n, uint32_t W) {
    uint32_t last = W & 31 ? (1u << (W & 31)) - 1 : ~0u;
    for (; n; n--) {
        uint32_t prev = 0;
        for (int w = 0; w < CW; w++) {
            uint32_t cur = d[w], next = w + 1 < CW ? d[w + 1] : 0;
            d[w] = cur | cur << 1 | cur >> 1 | prev >> 31 | next << 31;
            prev = cur;
        }
        d[(W - 1) >> 5] &= last;
        for (uint32_t w = ((W - 1) >> 5) + 1; w < CW; w++) d[w] = 0;
    }
}

/* 2. Row b of a' blur(S) per channel into slot b & 1 (rows outside the grid: its edge row), and of that less a' S
 * of the cell (b2), computed only in the cells within rmax of a source in their column of the window that the
 * add-back can read: a cell whose 3 x 3 neighbourhood is at the top of every channel (sm) feeds no group there. The
 * vertical pass runs in the cells with a source in their column within rmax of those; the rest of the slot is zero.
 * vt is zero outside the cells being computed. */
STAGE void blur_row(scr_t *s, uint32_t b) {
    const hp_t *p = &s->p;
    uint32_t W = s->W, H = s->H, R = p->rmax, o = b & 1;
    uint32_t *slots = s->bslot, m[CW] = { 0 }, d[CW], nd[CW], any = 0;
    for (uint32_t i = 0; i <= 2 * R; i++) {
        int32_t yy = (int32_t)b + (int32_t)i - (int32_t)R;
        yy = yy < 0 ? 0 : yy >= (int32_t)H ? (int32_t)H - 1 : yy;
        uint32_t k = slots[i] = s->slot[yy];
        for (int w = 0; w < CW; w++) m[w] |= s->tm[CW * k + w];
    }
    FOR_CELLS(s->zm[o], x) {                            /* clear what the slot held (row b - 2) */
        for (int c = 0; c < 3; c++) s->b[c][o * W + x] = 0, s->b2[c][o * W + x] = 0;
    }
    for (int w = 0; w < CW; w++) any |= d[w] = m[w];
    if (any) {
        const uint32_t *s0 = s->sm + CW * s->slot[b ? b - 1 : 0], *s1 = s->sm + CW * s->slot[b];
        const uint32_t *s2 = s->sm + CW * s->slot[b + 1 < H ? b + 1 : b];
        dilate(d, R, W);
        for (int w = 0; w < CW; w++) nd[w] = ~(s0[w] & s1[w] & s2[w]);
        dilate(nd, 1, W);
        any = 0;
        for (int w = 0; w < CW; w++) any |= d[w] &= nd[w];
        if (any) {
            for (int w = 0; w < CW; w++) nd[w] = d[w];
            dilate(nd, R, W);
            for (int w = 0; w < CW; w++) m[w] &= nd[w];
        }
    }
    for (int w = 0; w < CW; w++) s->zm[o][w] = any ? d[w] : 0;
    s->bxl[o] = W;
    s->bxr[o] = 0;
    if (!any) return;
    uint32_t hc[CW];                                    /* cells the horizontal pass computes */
    for (int w = 0; w < CW; w++) hc[w] = d[w];
    if (p->hdec) {                                      /* even cells, for every cell of d: their cubic fill */
        dilate(hc, 3, W);
        for (int w = 0; w < CW; w++) hc[w] &= 0x55555555u;
    }
    for (int c = 0; c < (p->bg ? 2 : 3); c++) {
        uint32_t a = p->a[c];
        if (!a) continue;
        uint16_t *out = s->b[c] + o * W;
        int16_t *out2 = s->b2[c] + o * W;
        const uint16_t **rows = s->brow;
        for (uint32_t i = 0; i <= 2 * R; i++) rows[i] = s->s[c] + slots[i] * W;
        int32_t *ho = s->ho, *v0 = s->vt, *v1 = v0 + W, *v2 = v1 + W;
        FOR_CELLS(hc, x) { ho[x] = 0; }
        {
            const int32_t *k0 = p->kv[0], *k1 = p->kv[1], *k2 = p->kv[2];
            uint32_t r0 = p->rad[0], r1 = p->rad[1], r2 = p->rmax;   /* taps past a radius are 0 */
            FOR_CELLS(m, x) {                           /* the bounces' vertical taps at once: each row read once */
                int32_t v = rows[R][x], a0 = k0[0] * v, a1 = k1[0] * v, a2 = k2[0] * v;
                uint32_t i = 1;
                for (; i <= r0; i++) {
                    v = rows[R - i][x] + rows[R + i][x];
                    a0 += k0[i] * v, a1 += k1[i] * v, a2 += k2[i] * v;
                }
                for (; i <= r1; i++) {
                    v = rows[R - i][x] + rows[R + i][x];
                    a1 += k1[i] * v, a2 += k2[i] * v;
                }
                for (; i <= r2; i++) a2 += k2[i] * (rows[R - i][x] + rows[R + i][x]);
                v0[x] = (a0 + 8192) >> 14, v1[x] = (a1 + 8192) >> 14, v2[x] = (a2 + 8192) >> 14;
            }
        }
        for (int k = 0; k < NB; k++) {
            uint32_t rk = p->rad[k];
            const int32_t *kh = p->kh[k], *vt = s->vt + k * W;
            FOR_CELLS(hc, x) {                          /* reads within rmax of a column of m */
                int32_t acc = kh[0] * vt[x];
                if (x >= rk && x + rk < W) {
                    for (uint32_t j = 1; j <= rk; j++) acc += kh[j] * (vt[x - j] + vt[x + j]);
                } else {
                    for (uint32_t j = 1; j <= rk; j++) {
                        uint32_t lx = x >= j ? x - j : 0, rx = x + j < W ? x + j : W - 1;
                        acc += kh[j] * (vt[lx] + vt[rx]);
                    }
                }
                ho[x] += acc;
            }
        }
        FOR_CELLS(m, x) { v0[x] = 0, v1[x] = 0, v2[x] = 0; }   /* back to zero for the next channel / row */
        uint32_t le = (W - 1) & ~1u;                    /* last even cell */
        uint32_t ra = R ? R - 1 : 0, rb = R ? R + 1 : 0;   /* rows b - 1, b, b + 1 (rmax 0: b) */
        const uint16_t *q0 = s->so[c] + slots[ra] * W, *q1 = s->so[c] + slots[R] * W;
        const uint16_t *q2 = s->so[c] + slots[rb] * W;   /* the cells' level */
        const uint16_t *s0 = rows[ra], *s1 = rows[R], *s2 = rows[rb];   /* and their source */
        FOR_CELLS(d, x) {
            int32_t hv = ho[x];
            if (p->hdec && (x & 1)) {                   /* cubic between the even cells around */
                uint32_t x1 = x + 1 <= le ? x + 1 : le, x3 = x + 3 <= le ? x + 3 : le, xm3 = x >= 3 ? x - 3 : 0;
                hv = 9 * ((ho[x - 1] >> 4) + (ho[x1] >> 4)) - (ho[xm3] >> 4) - (ho[x3] >> 4);   /* no overflow */
                if (hv < 0) hv = 0;
            }
            uint32_t v = (uint32_t)((hv + 8192) >> 14);
            v = (v * a + (2048u >> p->sh)) >> (12 - p->sh);   /* back to Q4 */
            if (v > H_MAX) v = H_MAX;
            /* The hold, of its 3 x 3 cells' max(level, source): their mean, at most twice their least (an even lit
             * area holds its own halo; next to a dark area, so in a lamp's ring, it is low); and the cells' level L
             * the same way, of their mean E (hx_group) */
            uint32_t xl = x ? x - 1 : 0, xr = x + 1 < W ? x + 1 : W - 1, hm = 65535, hs = 0, lm = 65535, ls = 0;
            const uint16_t *qr[3] = { q0, q1, q2 }, *sr[3] = { s0, s1, s2 };
            for (int r = 0; r < 3; r++) {
                const uint16_t *qq = qr[r], *ss = sr[r];
                uint32_t a0 = qq[xl], a1 = qq[x], a2 = qq[xr];
                if (a0 < lm) lm = a0;
                if (a1 < lm) lm = a1;
                if (a2 < lm) lm = a2;
                ls += a0 + a1 + a2;
                if (ss[xl] > a0) a0 = ss[xl];
                if (ss[x] > a1) a1 = ss[x];
                if (ss[xr] > a2) a2 = ss[xr];
                if (a0 < hm) hm = a0;
                if (a1 < hm) hm = a1;
                if (a2 < hm) hm = a2;
                hs += a0 + a1 + a2;
            }
            hs = (hs * 1821u) >> 14;
            hm = hs > 2 * hm ? 2 * hm : hs;
            int32_t own = (int32_t)((hm * OWN_K(a) + (2048u >> p->sh)) >> (12 - p->sh));
            int32_t v2 = (int32_t)v - own;
            out2[x] = (int16_t)(v2 < -32768 ? -32768 : v2);   /* less the hold */
            ls = (ls * 1821u) >> 14;
            lm = (ls > 2 * lm ? 2 * lm : ls) << p->sh;
            out[x] = (uint16_t)(lm > 16368 ? 16368 : lm);   /* at most the top (1023, Q4) */
            if (v2 >= H_MIN) {
                if (x < s->bxl[o]) s->bxl[o] = x;
                if (x > s->bxr[o]) s->bxr[o] = x;
            }
        }
    }
    if (p->bg) {                                        /* blue: green's halo x a'_B / a'_G */
        uint16_t *ob = s->b[2] + o * W;
        int16_t *ob2 = s->b2[2] + o * W;
        const int16_t *og2 = s->b2[1] + o * W;
        FOR_CELLS(d, x) {
            ob[x] = 0;                                  /* no level: all of its halo */
            ob2[x] = (int16_t)((og2[x] * (int32_t)p->rbg + 2048) >> 12);
        }
    }
}

/* 3. Full-resolution rows y0 <= y < y1 between the centres of cell rows b - 1 and b (o0, o1: slots of their blurred
 * rows; m0, m1: ring slots of their least codes; o0 == o1 above the first / below the last centre), only in the
 * groups next to cells with a halo. */
STAGE void addback_rows(scr_t *s, const frame_t *f, uint32_t y0, uint32_t y1, uint32_t o0, uint32_t o1,
                         uint32_t m0, uint32_t m1) {
    const hp_t *p = &s->p;
    uint32_t W = s->W;
    uint32_t cl = s->bxl[o0] < s->bxl[o1] ? s->bxl[o0] : s->bxl[o1];
    uint32_t cr = s->bxl[o0] <= s->bxr[o0] ? s->bxr[o0] : 0;
    if (s->bxl[o1] <= s->bxr[o1] && s->bxr[o1] > cr) cr = s->bxr[o1];
    if (cl > cr || y0 >= y1) return;
    uint32_t gl = cl ? cl - 1 : 0, gr = cr + 1 < W ? cr + 1 : W - 1;   /* groups whose spans touch [cl, cr] */
    uint32_t nact = 0;
    for (uint32_t sp = gl; sp <= gr + 1; sp++) {
        uint32_t j0 = sp ? sp - 1 : 0, j1 = sp < W ? sp : W - 1, mcm[3];
        for (int c = 0; c < 3; c++) {                   /* least code per channel of the 2 x 2 cells */
            const uint8_t *mc0 = s->mc[c] + m0 * W, *mc1 = s->mc[c] + m1 * W;
            uint32_t k = mc0[j0];
            if (mc0[j1] < k) k = mc0[j1];
            if (mc1[j0] < k) k = mc1[j0];
            if (mc1[j1] < k) k = mc1[j1];
            mcm[c] = k;
        }
        /* x = h2 x fade (hx_group) <= the cells' largest h2: a span that cannot reach a step is left (inside a lit
         * area h2 is below 0) */
        int act = 0;
        for (int c = 0; c < 3 && !act; c++) {
            const int16_t *c0 = s->b2[c] + o0 * W, *c1 = s->b2[c] + o1 * W;
            int32_t bm2 = c0[j0];
            if (c0[j1] > bm2) bm2 = c0[j1];
            if (c1[j0] > bm2) bm2 = c1[j0];
            if (c1[j1] > bm2) bm2 = c1[j1];
            uint32_t st = p->step[c][mcm[c]];
            act = st != NO_STEP && bm2 >= (int32_t)st;
        }
        s->span[sp] = (uint8_t)act;
        nact += (uint32_t)act;
    }
    if (!nact) return;
    uint32_t ng = 0;
    const uint32_t *sm0 = s->sm + CW * m0, *sm1 = s->sm + CW * m1;
    for (uint32_t g = gl; g <= gr; g++)                 /* not both cells at the top: nothing there would change */
        if ((s->span[g] | s->span[g + 1]) && !((sm0[g >> 5] & sm1[g >> 5]) >> (g & 31) & 1))
            s->glist[ng++] = (uint16_t)g;
    /* The halo of pair j of group g at row y: Q8 horizontal interpolation on the two cell rows (weights: the pair
     * centres 1, 3, 5, 7 px into the group, cell centres 3.5 px into theirs; pairs 0, 1 between cells g - 1, g, pairs
     * 2, 3 between g, g + 1), then the vertical one in Q16, stepped one row at a time by hx_group(): row centre
     * y + 0.5, cell-row centres 8 b + 3.5, weight of the lower row (y - 3) mod 8 eighths (0 at the frame's edges). */
    int32_t wy0 = o0 == o1 ? 0 : (int32_t)((y0 - 3) & 7) << 5, dw = o0 == o1 ? 0 : 32;
    for (uint32_t q = 0; q < ng; q++) {
        uint32_t g = s->glist[q], jl = g ? g - 1 : 0, jr = g + 1 < W ? g + 1 : W - 1;
        int32_t *hp = s->hp + 48 * q;
        for (int f2 = 0; f2 < 2; f2++)
            for (int c = 0; c < 3; c++) {
                int32_t a0, m0, z0, a1, m1, z1;
                if (f2) {
                    const int16_t *r0 = s->b2[c] + o0 * W, *r1 = s->b2[c] + o1 * W;
                    a0 = r0[jl], m0 = r0[g], z0 = r0[jr], a1 = r1[jl], m1 = r1[g], z1 = r1[jr];
                } else {
                    const uint16_t *r0 = s->b[c] + o0 * W, *r1 = s->b[c] + o1 * W;
                    a0 = r0[jl], m0 = r0[g], z0 = r0[jr], a1 = r1[jl], m1 = r1[g], z1 = r1[jr];
                }
                int32_t P0[4] = { 256 * a0 + 176 * (m0 - a0), 256 * a0 + 240 * (m0 - a0),
                                  256 * m0 + 48 * (z0 - m0), 256 * m0 + 112 * (z0 - m0) };
                int32_t P1[4] = { 256 * a1 + 176 * (m1 - a1), 256 * a1 + 240 * (m1 - a1),
                                  256 * m1 + 48 * (z1 - m1), 256 * m1 + 112 * (z1 - m1) };
                for (int j = 0; j < 4; j++) {
                    int32_t inc = (P1[j] - P0[j]) * dw;
                    hp[24 * f2 + 4 * c + j] = 256 * P0[j] + (P1[j] - P0[j]) * wy0 + 32768 - inc;   /* + inc first */
                    hp[24 * f2 + 12 + 4 * c + j] = inc;
                }
            }
    }
    for (uint32_t y = y0; y < y1; y++) {
        s->rows++;
        uint32_t *yp = (uint32_t *)(f->y + y * f->py);
        uint32_t *bp = (uint32_t *)(f->cb + y * f->pc), *rp = (uint32_t *)(f->cr + y * f->pc);
        static const uint8_t BAYER[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
        for (int j = 0; j < 4; j++) s->kern.dith[j] = BAYER[y & 3][j];
        uint32_t words = 0;
        for (uint32_t q = 0; q < ng; q++) {
            uint32_t g = s->glist[q];
            s->kern.hp = s->hp + 48 * q;
            words += hx_group(&s->kern, yp + 2 * g, bp + g, rp + g);
        }
        s->groups += ng;
        s->words += words;
    }
}

/* s->bw: the share (0 .. 64) of bright cells (s->big) in the square of 2 BIG_S + 1 cells around each cell (the
 * frame's edge cells repeated outside). Sources there lose the boost in proportion: a large lit area (a wall, the
 * sky) is not a lamp; the share changes smoothly, so does the halo. */
static void soft_map(scr_t *s) {
    uint32_t W = s->W, H = s->H, ww = (W + 31) >> 5, n = 2 * BIG_S + 1, any = 0;
    uint16_t *cs = s->cs;
    for (uint32_t i = 0; i < H * ww; i++) any |= s->big[i];
    if (!any) {                                         /* nothing bright: all boosted */
        uint32_t *bw = (uint32_t *)s->bw;
        for (uint32_t i = 0; i < (((H + 1) >> 1) * BW_W(W) + 3) >> 2; i++) bw[i] = 0;
        return;
    }
    for (uint32_t r = 0; r < H + BIG_S + 1; r++) {    /* horizontal counts of row r into ring slot r % BIG_N */
        uint32_t src = r < H ? r : H - 1, rany = 0;
        const uint32_t *row = s->big + src * ww;
        uint8_t *h = s->hring + (r % BIG_N) * W;
        for (uint32_t w = 0; w < ww; w++) rany |= row[w];
#define BIT(x) (row[(x) >> 5] >> ((x) & 31) & 1)
        uint32_t c = rany ? (BIG_S + 1) * BIT(0) : 0;
        for (uint32_t j = 1; rany && j <= BIG_S; j++) c += BIT(j < W ? j : W - 1);
        for (uint32_t x = 0; x < W; x++) {
            h[x] = (uint8_t)c;
            if (!rany) continue;
            uint32_t xin = x + BIG_S + 1 < W ? x + BIG_S + 1 : W - 1, xout = x >= BIG_S ? x - BIG_S : 0;
            c += BIT(xin) - BIT(xout);
        }
#undef BIT
        if (r < BIG_S) continue;
        uint32_t o = r - BIG_S;                         /* row o: rows o - BIG_S .. o + BIG_S (clamped) */
        if (o == 0) {
            for (uint32_t x = 0; x < W; x++) {
                uint32_t v = BIG_S * s->hring[x];       /* row 0 for the rows above */
                for (uint32_t j = 0; j <= BIG_S; j++) v += s->hring[(j % BIG_N) * W + x];
                cs[x] = (uint16_t)v;
            }
        } else {
            const uint8_t *in = s->hring + (r % BIG_N) * W;
            const uint8_t *out = s->hring + ((o > BIG_S ? o - BIG_S - 1 : 0) % BIG_N) * W;
            for (uint32_t x = 0; x < W; x++) cs[x] = (uint16_t)(cs[x] + in[x] - out[x]);
        }
        if (o >= H) break;
        if (o & 1) continue;                            /* kept on a 2 x 2 cell grid: it is smooth */
        uint8_t *bw = s->bw + (o >> 1) * BW_W(W);
        for (uint32_t x = 0; x < W; x += 2) bw[x >> 1] = (uint8_t)((cs[x] * 64u + n * n / 2) / (n * n));
    }
}

/* Cells within r (< 32) of a set cell of bits (H rows of (W + 31) / 32 words) and not in ex (the same layout, or
 * NULL), counted row by row until past lim. */
static uint32_t count_near(const uint32_t *bits, const uint32_t *ex, uint32_t W, uint32_t H, uint32_t r,
                           uint32_t lim) {
    uint32_t ww = (W + 31) >> 5, n = 0, v[CW], last = W & 31 ? (1u << (W & 31)) - 1 : ~0u;
    for (uint32_t i = 0; i < H && n <= lim; i++) {
        uint32_t i0 = i >= r ? i - r : 0, i1 = i + r < H ? i + r : H - 1, any = 0;
        for (uint32_t w = 0; w < ww; w++) v[w] = 0;
        for (uint32_t k = i0; k <= i1; k++)
            for (uint32_t w = 0; w < ww; w++) any |= v[w] |= bits[k * ww + w];
        if (!any) continue;
        for (uint32_t done = 0, sh = 1; done < r; done += sh, sh <<= 1) {   /* by 1, 2, 4, ..: r in log2 r passes */
            if (sh > r - done) sh = r - done;
            uint32_t prev = 0;
            for (uint32_t w = 0; w < ww; w++) {
                uint32_t cur = v[w], next = w + 1 < ww ? v[w + 1] : 0;
                v[w] = cur | cur << sh | cur >> sh | prev >> (32 - sh) | next << (32 - sh);
                prev = cur;
            }
        }
        v[ww - 1] &= last;
        if (ex)
            for (uint32_t w = 0; w < ww; w++) v[w] &= ~ex[i * ww + w];
        for (uint32_t w = 0; w < ww; w++) {
            uint32_t c = v[w] - ((v[w] >> 1) & 0x55555555u);
            c = (c & 0x33333333u) + ((c >> 2) & 0x33333333u);
            n += (((c + (c >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
        }
    }
    return n;
}

/* 0. Cost estimate, before anything is written: the first sample row of every cell row through the pre-test, the
 * groups that pass decoded for a source; the source cells, dilated by rmax (the blur's reach), bound the cells with
 * a halo; those near a boosted source (its bright share under 1/2: a lamp, not a lit wall) bound where the halo
 * shows (the add-back's work). Returns 0 if the pass fits HX_BUDGET (stops counting as soon as it cannot), else 1;
 * r->dec, r->src, r->nh, r->nb: decoded groups, source cells, cells with a halo, cells near a boosted source
 * counted so far. Costs: the camera's ms per emulator instruction (H2), the emulator's counts (tests/emu). */
STAGE uint32_t over_budget(scr_t *s, const frame_t *f, volatile hx_rec_t *r) {
    const hp_t *p = &s->p;
    uint32_t W = s->W, H = s->H, R = p->rmax, ww = (W + 31) >> 5, dec = 0, src = 0, all = p->all;
    uint32_t *em = s->em, *bg = s->big;
    const int32_t M0 = p->m[0], M1 = p->m[1], M2 = p->m[2], M3 = p->m[3], M4 = p->m[4], M5 = p->m[5];
    const int32_t M6 = p->m[6], M7 = p->m[7], M8 = p->m[8];
    const uint16_t *t0 = p->tinv[0], *t1 = p->tinv[1], *t2 = p->tinv[2];
    for (uint32_t i = 0; i < H; i++) {
        uint32_t *row = em + i * ww, *brow = bg + i * ww;
        for (uint32_t w = 0; w < ww; w++) row[w] = brow[w] = 0;
        const uint32_t *yp = (const uint32_t *)(f->y + (CELL * i + 2) * f->py);   /* source_row()'s first row */
        const uint32_t *bp = (const uint32_t *)(f->cb + (CELL * i + 2) * f->pc);
        const uint32_t *rp = (const uint32_t *)(f->cr + (CELL * i + 2) * f->pc);
        if (f->h - CELL * i <= 2) {
            yp = (const uint32_t *)(f->y + CELL * i * f->py);
            bp = (const uint32_t *)(f->cb + CELL * i * f->pc);
            rp = (const uint32_t *)(f->cr + CELL * i * f->pc);
        }
        uint32_t end = all ? W : 0;
        for (uint32_t g = next_group(yp, bp, rp, 0, W, p, &end); g < W;
             g = next_group(yp, bp, rp, g + 1, W, p, &end)) {
            dec++;
            uint32_t y01 = yp[2 * g], y23 = yp[2 * g + 1], cbw = bp[g], crw = rp[g];
            uint32_t nb = 0, nbr = 0;
            for (int j = 1; j < 4; j += 2) {
                int32_t yy = (int32_t)(((j < 2 ? y01 : y23) >> ((j & 1) ? 8 : 24)) & 0xFF);
                int32_t cb = (int32_t)((cbw >> (24 - 8 * j)) & 0xFF) - 128;
                int32_t cr = (int32_t)((crw >> (24 - 8 * j)) & 0xFF) - 128;
                uint32_t er = t0[clamp255((M0 * yy + M1 * cb + M2 * cr + 2048) >> 12)];
                uint32_t eg = t1[clamp255((M3 * yy + M4 * cb + M5 * cr + 2048) >> 12)];
                uint32_t eb = t2[clamp255((M6 * yy + M7 * cb + M8 * cr + 2048) >> 12)];
                uint32_t mx = er > eg ? er : eg;
                mx = (mx > eb ? mx : eb) >> 4;
                nb |= mx > E_LO;                        /* a source (some channel's gain > 0) */
                nbr += mx >= E_BIG;
            }
            if (nb) {
                row[g >> 5] |= 1u << (g & 31);
                src++;
            }
            if (nbr == 2) brow[g >> 5] |= 1u << (g & 31);
        }
        /* over already: decoding (one source row at least), and the source cells (a lower bound of the cells with a
         * halo) */
        if (dec > (g_hx_budget - EST_FIX) / (EST_DEC + EST_ROW) ||
            src > (g_hx_budget - EST_FIX - dec * (EST_DEC + EST_ROW)) / EST_CELL) {
            r->dec = dec;
            r->src = src;
            return 1;
        }
    }
    s->ny1 = dec > NY1_DEC;
    r->dec = dec;
    r->src = src;
    uint32_t cd = dec * (EST_DEC + (s->ny1 ? 1 : 2) * EST_ROW);
    if (cd > g_hx_budget - EST_FIX) return 1;
    uint32_t left = g_hx_budget - EST_FIX - cd;
    uint32_t nh = count_near(em, NULL, W, H, R, left / EST_CELL);
    r->nh = nh;
    if (nh > left / EST_CELL) return 1;
    soft_map(s);                                        /* s->big is free after it: the boosted bright sources */
    for (uint32_t i = 0; i < H; i++) {
        const uint8_t *q = s->bw + (i >> 1) * BW_W(W);
        for (uint32_t w = 0; w < ww; w++) {
            uint32_t b = em[i * ww + w] & bg[i * ww + w], o = 0;
            for (uint32_t j = 0; j < 32 && b >> j; j++)
                if (b >> j & 1 && q[(32 * w + j) >> 1] < 32) o |= 1u << j;
            bg[i * ww + w] = o;
        }
    }
    left -= nh * EST_CELL;
    uint32_t nb = count_near(bg, em, W, H, R, left / EST_BOOST);   /* on dark ground: where a halo shows */
    r->nb = nb;
    if (nb > left / EST_BOOST) return 1;
    return 0;
}

static void pass(scr_t *s, const frame_t *f) {
    uint32_t H = s->H, R = s->p.rmax;
    for (int c = 0; c < 3; c++) {
        s->kern.t[c] = s->p.t[c];
        s->kern.ts[c] = s->p.ts[c];
        s->kern.a[c] = OWN_K(s->p.a[c]);
    }
    for (int i = 0; i < 9; i++) {
        s->kern.m[i] = s->p.m[i];
        s->kern.c[i] = s->p.c[i];
    }
    hx_src_t *sk = &s->src;
    for (int i = 0; i < 9; i++) sk->m[i] = s->p.m[i];
    for (int c = 0; c < 3; c++) {
        sk->tinv[c] = s->p.tinv[c];
        sk->top[c] = s->p.top[c];
        sk->bmin[c] = s->bmin[c];
        sk->acc[c] = s->acc + c * s->W;
        sk->acc2[c] = s->acc2 + c * s->W;
    }
    sk->gain = s->p.gain;
    sk->gunb = s->p.gunb;
    s->kern.fade = s->p.fade;
    s->kern.rec = s->p.rec;
    sk->sh9 = 9 + s->p.sh;
    sk->fl = (s->p.a[0] ? 1u : 0) | (s->p.a[1] ? 2u : 0) | (s->p.a[2] && !s->p.bg ? 4u : 0);
    sk->npass = s->npass;
    sk->nsat = s->nsat;
    for (uint32_t i = 0, k = 0; i < H; i++) {         /* ring slot of each cell row */
        s->slot[i] = (uint8_t)k;
        k = k + 1 == s->ring ? 0 : k + 1;
    }
    for (uint32_t i = 0; i < H + R; i++) {
        if (i < H) source_row(s, f, i);
        if (i < R) continue;
        uint32_t b = i - R, o = b & 1, m = s->slot[b];
        blur_row(s, b);
        if (b == 0)                                     /* rows above the first centre: row 0 only */
            addback_rows(s, f, 0, f->h < 3 ? f->h : 3, o, o, m, m);
        else
            addback_rows(s, f, CELL * b - 5, CELL * b + 3 < f->h ? CELL * b + 3 : f->h, o ^ 1, o, s->slot[b - 1], m);
        if (b == H - 1 && CELL * b + 3 < f->h)           /* rows below the last centre */
            addback_rows(s, f, CELL * b + 3, f->h, o, o, m, m);
    }
}

static uint32_t gate(uint32_t ctx, const hp_t *p) {
    if (CTX8(0x408) != p->cm || CTX8(0x357) != 0) return HX_MODE;
    if (CTX8(0x008) == GR2_FX_CTX_DNG || CTX32(0x1E0) != 0) return HX_RAW;
    uint32_t sc = GR2_FN_PROP_GET(GR2_PROPID_SHOOTING_CONTEXT);
    if (sc == 1 || sc == 3) return HX_CONTEXT;
    return HX_OK;
}

static uint32_t run(uint32_t ctx, volatile hx_rec_t *r) {
    if (!g_valid) return HX_TABLES;
    uint32_t seq = g_seq;
    if (seq & 1) return HX_TABLES;
    if (CTX8(0x408) != g_p.cm) return HX_MODE;     /* cheap first: most frames of other modes stop here */
    frame_t f;
    uint32_t st = frame(ctx, &f);
    if (st != HX_OK) return st;
    r->y = f.y;
    uint32_t h = GR2_FN_IMGBUF_ALLOC(GR2_IMGBUF_SPRWORK, 0, FILE_TAG, __LINE__);
    if (!h) return HX_NO_SCRATCH;
    st = HX_NO_SCRATCH;
    const volatile uint8_t *type = buf_type(GR2_IMGBUF_SPRWORK);
    uint32_t base = (h & 3) || !DRAM(h) || *(const volatile uint32_t *)h != GR2_IMGBUF_MAGIC ? 0 :
                    cached(*(const volatile uint32_t *)(h + 4));
    uint32_t size = type ? *(const volatile uint32_t *)(type + 4) : 0;
    scr_t *s = 0;
    if (base && !(base & 3) && size && DRAM(base) && DRAM(base + size - 1) && base + size > base)
        st = layout(&s, base, size, &f);
    if (st == HX_OK) {
        r->scratch = base;
        const volatile uint32_t *src = (const volatile uint32_t *)&g_p;
        uint32_t *dst = (uint32_t *)&s->p;
        for (uint32_t i = 0; i < sizeof(hp_t) / 4; i++) dst[i] = src[i];
        if (g_seq != seq || !g_valid) st = HX_TABLES;   /* rewritten meanwhile */
        else st = gate(ctx, &s->p);
    }
    if (st == HX_OK) {
        s->cells = s->rows = s->groups = s->words = 0;
        uint32_t t0 = GR2_FN_SYSTEM_GETTIME();
        if (over_budget(s, &f, r)) st = HX_BUSY;
        else {
            pass(s, &f);
            if (!s->words) st = HX_NONE;                /* no halo reached a code: the frame is as it was */
        }
        r->t_pass = GR2_FN_SYSTEM_GETTIME() - t0;
        r->cells = s->cells;
        r->rows = s->rows;
        r->groups = s->groups;
        r->words = s->words;
    }
    GR2_FN_IMGBUF_FREE(&h, FILE_TAG, __LINE__);
    return st;
}

void halation_hook(uint32_t ctx) {
    uint32_t site = (uint32_t)__builtin_return_address(0);
    uint32_t t0 = GR2_FN_SYSTEM_GETTIME();
    ((void (*)(uint32_t))(GR2_FX_STEP | 1))(ctx);
    if (!g_valid) return;                           /* off (BL being restored, or a stale I-cache line) */
    volatile hx_rec_t *r = &g_rec;
    uint32_t seq = r->seq;
    r->seq = seq | 1;
    r->t_stock = GR2_FN_SYSTEM_GETTIME() - t0;
    r->shot = ++g_shots;
    r->site = site;
    r->ctx = ctx;
    r->mode = CTX8(0x408) | (uint32_t)CTX8(0x357) << 8 | (uint32_t)CTX8(0x008) << 16;
    r->raw = CTX32(0x1E0);
    r->wh = CTX32(0x258);
    r->y = r->scratch = r->t_pass = 0;
    r->cells = r->rows = r->groups = r->words = r->dec = r->nh = r->src = r->nb = 0;
    r->status = run(ctx, r);
    r->seq = (seq | 1) + 1;
}

/* ---- report (CFW task) ---------------------------------------------------------------------- */

void halation_poll(void) {
    uint32_t seq = g_rec.seq;
    if ((seq & 1) || seq == g_logged_seq) return;
    hx_rec_t c;
    const volatile uint32_t *src = (const volatile uint32_t *)&g_rec;
    uint32_t *dst = (uint32_t *)&c;
    for (uint32_t i = 0; i < sizeof(c) / 4; i++) dst[i] = src[i];
    if (g_rec.seq != seq) return;                   /* a newer shot was being written: next poll */
    g_logged_seq = seq;
    if (c.status >= HX_FIRST_ERROR)
        ERR_HEX2("halation: frame left untouched (status 7 buffer 8 table 9 chroma 10 size 11 outside 12 align "
                 "13 scratch busy 14 scratch size, ctx)", c.status, c.ctx);
    if (c.shot > g_logged_shot + 1) DBG_HEX("halation: shots not logged (overwritten)", c.shot - g_logged_shot - 1);
    g_logged_shot = c.shot;
    DBG_HEX2("halation: shot (n, status 0 done 1 tables 2 mode 3 RAW/DNG 4 interval/multi 5 no highlights "
             "6 over budget)", c.shot, c.status);
    DBG_HEX2("halation: (caller, ctx +0x408 | +0x357 << 8 | +0x008 << 16)", c.site, c.mode);
    DBG_HEX2("halation: (ctx +0x1E0 RAW, +0x258 w | h << 16)", c.raw, c.wh);
    DBG_HEX2("halation: (Y plane, scratch)", c.y, c.scratch);
    DBG_HEX2("halation: ms (stock step, pass)", c.t_stock, c.t_pass);
    DBG_HEX2("halation: (source cells, rows with a halo)", c.cells, c.rows);
    DBG_HEX2("halation: (8-px groups, words written)", c.groups, c.words);
    DBG_HEX2("halation: estimate (groups decoded, cells with a halo)", c.dec, c.nh);
    DBG_HEX2("halation: estimate (source cells, cells near a boosted source)", c.src, c.nb);
    DBG_HEX("halation: estimate ms on the camera", udiv32(EST_FIX + c.dec * (EST_DEC + (c.dec > NY1_DEC ? 1 : 2) *
            EST_ROW) + c.nh * EST_CELL + c.nb * EST_BOOST, 1000));
}
