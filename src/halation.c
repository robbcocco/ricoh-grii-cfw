/* Film halation in final stills (finding 37 §1, §4.1; M17). All variants.
 *
 * Model (spektrafilm `apply_halation_um`, its defaults): exposure E2 = (E + a * sum_k w_k G(sigma sqrt k) * E) / (1 + a)
 * per channel, 3 bounces, w = 4/7, 2/7, 1/7. The recipe look is fitted to spektrafilm with halation off, so the
 * pass adds only the spatial part, a' (G * E - E) with a' = a / (1 + a), and only where it is positive: the camera's
 * sources are clipped, so the small loss spektrafilm gives the source itself is not placed. The source is the
 * highlights only: S = E w(E), w ramping from 0 at E_LO to 1 at E_HI (2 and 3 EV above mid grey in the tone-curve
 * domain); frames without highlights are left byte-identical.
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
#define RMAX        12                 /* kernel radius, cells (LOOK_HALO_UM_MAX fits) */
#define MAX_W       624                /* cells per row: frames up to 4992 px wide */
#define TILE        8                  /* blur tile, cells */
#define TW          ((MAX_W / TILE + 31) / 32)   /* words of a tile mask */
#define NBOX        4                  /* source pre-test boxes (src/halation_arm.S hx_scan4 tests 4) */
#define MAX_H       432                /* cell rows: up to 3456 px high */
#define E_LO        400                /* source ramp, linear units (1023 = top of the tone curve) */
#define E_HI        800
#define H_MIN       8                  /* 1/2 linear unit, Q4: the least halo that is added */
/* Cost budget (over_budget()), instructions: 1.37 s at 146 MIPS, the rate H1's full Y read implies with its memory
 * traffic (finding 37: 117 ms [H] for a loop of 17 Thumb instructions per 4 words, 17.1 M instructions, 16 MB); the
 * cost model is fitted to the emulator's counts at 3:2 (night, busy night, bright, bright worst case; tests/emu
 * halation timing, within 2 %). A frame over it is left without halation. */
#define HX_BUDGET   200000000u
#define EST_FIX     33000000u          /* the pre-test scans, the estimate, the empty rows' blur and bookkeeping */
#define EST_DEC     400u               /* a group of a sampled row passing the pre-test (decoding) */
#define EST_CELL    6600u              /* a cell within rmax of a source (blur, 8 rows of add-back) */
#define NO_STEP     0xFFFFu
/* Variance (px^2, Q8) the grid adds: sampling (4 samples 2 px apart: 4 * 15 / 12) + bilinear upsampling (8^2 / 6). */
#define V_GRID_Q8   4011
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
    uint32_t recip[17];                /* 65536 / n samples */
    uint32_t a[3];                     /* a' = a / (1 + a), Q12 */
    uint32_t rad[NB], rmax;            /* kernel radius per bounce, largest */
    int32_t kv[NB][RMAX + 1];          /* vertical taps, bounce weight folded in, Q14 */
    int32_t kh[NB][RMAX + 1];          /* horizontal taps, Q14 */
    uint16_t tinv[3][256];             /* code -> linear, Q4 (centre of the code's interval) */
    uint16_t src[3][256];              /* code -> source S = E w(E), linear units */
    uint16_t sa[3][256];               /* code -> a' S, Q4 */
    uint16_t step[3][256];             /* least halo (Q4) that changes a code >= k, NO_STEP: none */
    uint32_t ts[3][256];               /* tinv << 16 | sa (src/halation_arm.S) */
    uint8_t t[3][2048];                /* tone curves (made monotone), index 1024.. = the top: no clamp */
} hp_t;

static hp_t g_p;
static volatile uint32_t g_seq;        /* odd while g_p is written */
static volatile uint32_t g_valid;      /* g_p describes the shown look */
static uint32_t g_key[8];              /* inputs g_p was built from */
static uint32_t g_veneer;
static volatile uint32_t *g_target;
static int g_on, g_refused;

/* ---- shot record (the hook writes, the CFW task logs) ------------------------------------------ */

enum {
    HX_OK, HX_TABLES, HX_MODE, HX_RAW, HX_CONTEXT,                  /* normal: applied / not for this frame */
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
    uint32_t dec, nh;                  /* estimate: groups decoded, cells with a halo (nh 0: stopped early) */
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

/* Symmetric discrete kernel of variance v (cells^2, Q8): n stages of [a, 1 - 2a, a], a = v / 2n <= 1/4, Q14. */
static uint32_t kernel(uint32_t v_q8, int32_t *k) {
    uint32_t n = (2 * v_q8 + 255) >> 8;
    if (n > RMAX) { n = RMAX; v_q8 = RMAX * 128; }
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

static int build(const look_t *l, int k5, uint32_t cm, uint32_t con, uint32_t sat, uint32_t adobe) {
    hp_t *p = &g_p;
    int16_t ycc[9];
    static uint8_t tone[3][1024];
    if (look_still_model(k5, con, sat, adobe, tone, ycc) != 0) return -1;
    for (int i = 0; i < 9; i++) p->c[i] = ycc[i];
    if (invert(ycc, p->m) != 0) return -1;
    p->cm = cm;
    uint32_t code0 = 256;
    for (int c = 0; c < 3; c++) {
        uint8_t *t = p->t[c];
        for (int i = 0; i < 2048; i++) {                 /* monotone, extended */
            uint8_t v = tone[c][i < 1024 ? i : 1023];
            t[i] = i && v < t[i - 1] ? t[i - 1] : v;
        }
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
            uint32_t e = (centre + 8) >> 4, s = e >= E_HI ? e : e <= E_LO ? 0 : udiv32(e * (e - E_LO) + 200, 400);
            p->src[c][k] = (uint16_t)(p->a[c] ? s : 0);
            p->sa[c][k] = (uint16_t)((p->a[c] * s * 16 + 2048) >> 12);
            p->ts[c][k] = centre << 16 | p->sa[c][k];
            if (p->src[c][k] && k < code0) code0 = k;
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
    for (uint32_t n = 1; n <= 16; n++) p->recip[n] = udiv32(65536, n);
    uint32_t um = l->halo_um ? l->halo_um : LOOK_HALO_UM_DEF;
    int32_t wsum = 0;
    p->rmax = 0;
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
            GR2_FN_PROP_GET_RAW(GR2_PROPID_COLOR_SPACE), 1,
        };
        int same = 1;                                   /* g_key[7] = 1: built (or tried) from g_key */
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
    int32_t *hp;                       /* +144: the group's 12 accumulators (Q16) then their 12 row steps */
} hx_kern_t;
_Static_assert(sizeof(hx_kern_t) == 148, "hx_kern_t layout (src/halation_arm.S)");
uint32_t hx_group(hx_kern_t *k, uint32_t *y, uint32_t *cb, uint32_t *cr);   /* ARM */

typedef struct {
    hp_t p;                            /* the tables, copied */
    uint32_t W, H, ring;               /* cells (W = frame width / 8), rows held in the source ring */
    uint16_t *s[3];                    /* source ring: ring x W mean sources (Q4) per channel */
    uint8_t *mc;                       /* ring x W: least code seen in the cell (0 if a sample was not decoded) */
    uint32_t *tm;                      /* ring x TW: tiles with a source (bit t of word t / 32: tile t) */
    uint8_t *slot;                     /* H: ring slot of each cell row */
    uint16_t rl[RING], rr[RING];       /* cells a ring slot may hold non-zero (rl > rr: none) */
    uint16_t *b[3];                    /* 2 x W: a' blur(S) (Q4) of rows b - 1 and b, in slot b & 1 */
    uint32_t bxl[2], bxr[2];           /* that row: first / last cell with a halo >= H_MIN (bxl > bxr: none) */
    uint32_t zm[2][TW];                /* tiles of that slot that may be non-zero */
    uint16_t *glist;                   /* W: groups of the current band that can change */
    uint32_t *acc;                     /* 3 x W source sums of the cell row being sampled */
    uint8_t *bmin, *npass;             /* W: least code decoded, sample rows decoded */
    int32_t *vt, *ho;                  /* W: blur temporaries */
    int32_t *hp;                       /* 24 x W: hx_kern_t hp of each listed group */
    uint32_t *em;                      /* H x (W + 31) / 32: over_budget()'s source / halo cell bitmap */
    uint8_t *span;                     /* W + 1: 2 x 2 cell blocks that can change (span j: cells j - 1, j) */
    uint32_t cells, rows, groups, words;
    hx_kern_t kern;                    /* the add-back's tables and the group's halo */
} scr_t;

static uint32_t align4(uint32_t a) { return (a + 3) & ~3u; }

static uint32_t layout(scr_t **out, uint32_t base, uint32_t size, const frame_t *f) {
    uint32_t W = f->w >> 3, H = (f->h + CELL - 1) >> 3, R = RING;
    if (W > MAX_W || H > MAX_H) return HX_BAD_SIZE;
    uint32_t a = base;
    scr_t *s = (scr_t *)a;
    a = align4(a + sizeof(scr_t));
    for (int c = 0; c < 3; c++) { s->s[c] = (uint16_t *)a; a = align4(a + 2 * R * W); }
    s->mc = (uint8_t *)a; a = align4(a + R * W);
    s->tm = (uint32_t *)a; a += 4 * TW * R;
    s->slot = (uint8_t *)a; a = align4(a + H);
    for (int c = 0; c < 3; c++) { s->b[c] = (uint16_t *)a; a = align4(a + 4 * W); }
    s->acc = (uint32_t *)a; a += 12 * W;
    s->bmin = (uint8_t *)a; a = align4(a + W);
    s->npass = (uint8_t *)a; a = align4(a + W);
    s->vt = (int32_t *)a; a += 4 * W;
    s->ho = (int32_t *)a; a += 4 * W;
    s->hp = (int32_t *)a; a += 96 * W;
    s->em = (uint32_t *)a; a += 4 * H * ((W + 31) >> 5);
    s->span = (uint8_t *)a; a = align4(a + W + 1);
    s->glist = (uint16_t *)a; a = align4(a + 2 * W);
    if (a - base > size) return HX_SCRATCH_SIZE;
    s->W = W;
    s->H = H;
    s->ring = R;
    for (int o = 0; o < 2; o++) {
        s->bxl[o] = W;                                  /* empty */
        s->bxr[o] = 0;
        for (int w = 0; w < TW; w++) s->zm[o][w] = 0;
        for (int c = 0; c < 3; c++)
            for (uint32_t x = 0; x < W; x++) s->b[c][o * W + x] = 0;
    }
    for (uint32_t k = 0; k < R; k++) {                  /* the ring: unknown */
        s->rl[k] = 0;
        s->rr[k] = (uint16_t)(W - 1);
    }
    for (uint32_t x = 0; x < W; x++) s->vt[x] = 0;
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

/* First group >= g of the row that may hold a source, W if none: blocks of 4 groups (32 px) in ARM state
 * (src/halation_arm.S, the same test as hit()), then the groups of a block that answered yes one by one. */
static uint32_t scan(const uint32_t *yp, const uint32_t *bp, const uint32_t *rp, uint32_t g, uint32_t W,
                     const hp_t *p) {
    while (g < W) {
        g += 4 * hx_scan4(yp + 2 * g, bp + g, rp + g, p->bk, (W - g) >> 2);
        for (uint32_t end = g + 4 < W ? g + 4 : W; g < end; g++)
            if (hit(yp + 2 * g, bp + g, rp + g, p)) return g;
    }
    return W;
}

/* 1. Cell row i: the mean source (Q4) per channel and the least code seen, from the first pixel of every chroma
 * pair in rows 8i, 8i + 2, 8i + 4, 8i + 6 (4 x 4 samples; centroid 3.5 px into the cell = the cell centre). An
 * 8-px group is decoded only if hit() says so: build() chose its boxes so that no pixel inside one reaches a source
 * code in any channel. */
static void source_row(scr_t *s, const frame_t *f, uint32_t i) {
    const hp_t *p = &s->p;
    uint32_t W = s->W, y0 = CELL * i, slot = s->slot[i];
    uint32_t *a0 = s->acc, *a1 = a0 + W, *a2 = a1 + W;
    uint32_t all = p->all;
    uint32_t gl = W, gr = 0;                            /* groups decoded; [gl, W) is initialised */
    uint32_t rows = f->h - y0 < CELL ? f->h - y0 : CELL;
    for (uint32_t r = 0; r < rows; r += 2) {
        const uint32_t *yp = (const uint32_t *)(f->y + (y0 + r) * f->py);
        const uint32_t *bp = (const uint32_t *)(f->cb + (y0 + r) * f->pc);
        const uint32_t *rp = (const uint32_t *)(f->cr + (y0 + r) * f->pc);
        for (uint32_t g = all ? 0 : scan(yp, bp, rp, 0, W, p); g < W;
             g = all ? g + 1 : scan(yp, bp, rp, g + 1, W, p)) {
            uint32_t y01 = yp[2 * g], y23 = yp[2 * g + 1], cbw = bp[g], crw = rp[g];
            if (g < gl) {                               /* initialise up to what is initialised already */
                for (uint32_t x = g; x < gl; x++) {
                    a0[x] = a1[x] = a2[x] = 0;
                    s->npass[x] = 0;
                    s->bmin[x] = 255;
                }
                gl = g;
            }
            if (g > gr) gr = g;
            s->npass[g]++;
            uint32_t mn = s->bmin[g];
            for (int j = 0; j < 4; j++) {
                int32_t yy = (int32_t)(((j < 2 ? y01 : y23) >> ((j & 1) ? 8 : 24)) & 0xFF);
                int32_t cb = (int32_t)((cbw >> (24 - 8 * j)) & 0xFF) - 128;
                int32_t cr = (int32_t)((crw >> (24 - 8 * j)) & 0xFF) - 128;
                int32_t R = clamp255((p->m[0] * yy + p->m[1] * cb + p->m[2] * cr + 2048) >> 12);
                int32_t G = clamp255((p->m[3] * yy + p->m[4] * cb + p->m[5] * cr + 2048) >> 12);
                int32_t B = clamp255((p->m[6] * yy + p->m[7] * cb + p->m[8] * cr + 2048) >> 12);
                a0[g] += p->src[0][R];
                a1[g] += p->src[1][G];
                a2[g] += p->src[2][B];
                if ((uint32_t)R < mn) mn = (uint32_t)R;
                if ((uint32_t)G < mn) mn = (uint32_t)G;
                if ((uint32_t)B < mn) mn = (uint32_t)B;
            }
            s->bmin[g] = (uint8_t)mn;
        }
    }
    /* The ring row: zeros and mc 0 outside the decoded groups (only where the slot held something), inside the means
     * (a group not decoded in every sample row has mc 0). */
    uint32_t ny = (rows + 1) >> 1, rc = p->recip[4 * ny], tb[TW] = { 0 };
    uint16_t *d0 = s->s[0] + slot * W, *d1 = s->s[1] + slot * W, *d2 = s->s[2] + slot * W;
    uint8_t *mc = s->mc + slot * W;
    for (uint32_t x = s->rl[slot], xe = s->rr[slot]; x <= xe; x++) {
        if (x >= gl && x <= gr) { x = gr; continue; }
        d0[x] = d1[x] = d2[x] = 0;
        mc[x] = 0;
    }
    s->rl[slot] = (uint16_t)gl;
    s->rr[slot] = (uint16_t)gr;
    for (uint32_t x = gl; x <= gr; x++) {
        uint32_t v0 = (a0[x] * rc + 2048) >> 12, v1 = (a1[x] * rc + 2048) >> 12, v2 = (a2[x] * rc + 2048) >> 12;
        d0[x] = (uint16_t)v0;
        d1[x] = (uint16_t)v1;
        d2[x] = (uint16_t)v2;
        mc[x] = s->npass[x] == ny ? s->bmin[x] : 0;
        if (v0 | v1 | v2) {
            s->cells++;
            uint32_t t = x / TILE;
            tb[t >> 5] |= 1u << (t & 31);
        }
    }
    for (int w = 0; w < TW; w++) s->tm[TW * slot + w] = tb[w];
}

#define TILE_ON(m, t) (((m)[(t) >> 5] >> ((t) & 31)) & 1u)
/* 2. Row b of a' blur(S) per channel into slot b & 1 (rows outside the grid: its edge row), computed only in the
 * 8-cell tiles that hold a source in the window (vertical pass) and the tiles within rmax of those (horizontal
 * pass); the rest of the slot is zero. vt is zero outside the tiles being computed. */
static void blur_row(scr_t *s, uint32_t b) {
    const hp_t *p = &s->p;
    uint32_t W = s->W, H = s->H, R = p->rmax, o = b & 1, nt = (W + TILE - 1) / TILE;
    uint32_t slots[2 * RMAX + 1], m[TW] = { 0 }, d[TW], any = 0;
    for (uint32_t i = 0; i <= 2 * R; i++) {
        int32_t yy = (int32_t)b + (int32_t)i - (int32_t)R;
        yy = yy < 0 ? 0 : yy >= (int32_t)H ? (int32_t)H - 1 : yy;
        uint32_t k = slots[i] = s->slot[yy];
        for (int w = 0; w < TW; w++) m[w] |= s->tm[TW * k + w];
    }
    for (uint32_t t = 0; t < nt; t++) {                 /* clear what the slot held (row b - 2) */
        if (!TILE_ON(s->zm[o], t)) continue;
        uint32_t x1 = TILE * t + TILE < W ? TILE * t + TILE : W;
        for (int c = 0; c < 3; c++)
            for (uint32_t x = TILE * t; x < x1; x++) s->b[c][o * W + x] = 0;
    }
    for (int w = 0; w < TW; w++) any |= d[w] = m[w];
    for (uint32_t n = 0; n < (R + TILE - 1) / TILE; n++) {   /* dilate by rmax cells */
        uint32_t e[TW];
        for (int w = 0; w < TW; w++)
            e[w] = d[w] | d[w] << 1 | d[w] >> 1 | (w ? d[w - 1] >> 31 : 0) | (w + 1 < TW ? d[w + 1] << 31 : 0);
        for (int w = 0; w < TW; w++) d[w] = e[w];
    }
    for (int w = 0; w < TW; w++) s->zm[o][w] = d[w];
    s->bxl[o] = W;
    s->bxr[o] = 0;
    if (!any) return;
    for (int c = 0; c < 3; c++) {
        uint32_t a = p->a[c];
        if (!a) continue;
        uint16_t *out = s->b[c] + o * W;
        const uint16_t *rows[2 * RMAX + 1];
        for (uint32_t i = 0; i <= 2 * R; i++) rows[i] = s->s[c] + slots[i] * W;
        int32_t *vt = s->vt, *ho = s->ho;
        for (uint32_t t = 0; t < nt; t++)
            if (TILE_ON(d, t))
                for (uint32_t x = TILE * t, x1 = x + TILE < W ? x + TILE : W; x < x1; x++) ho[x] = 0;
        for (int k = 0; k < NB; k++) {
            uint32_t rk = p->rad[k];
            const int32_t *kv = p->kv[k], *kh = p->kh[k];
            for (uint32_t t = 0; t < nt; t++) {
                if (!TILE_ON(m, t)) continue;
                for (uint32_t x = TILE * t, x1 = x + TILE < W ? x + TILE : W; x < x1; x++) {
                    int32_t acc = kv[0] * rows[R][x];
                    for (uint32_t i = 1; i <= rk; i++) acc += kv[i] * (rows[R - i][x] + rows[R + i][x]);
                    vt[x] = (acc + 8192) >> 14;
                }
            }
            for (uint32_t t = 0; t < nt; t++) {
                if (!TILE_ON(d, t)) continue;
                uint32_t x1 = TILE * t + TILE < W ? TILE * t + TILE : W;
                for (uint32_t x = TILE * t; x < x1; x++) {   /* reads within rmax of the tile */
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
            for (uint32_t t = 0; t < nt; t++)           /* back to zero for the next bounce / row */
                if (TILE_ON(m, t))
                    for (uint32_t x = TILE * t, x1 = x + TILE < W ? x + TILE : W; x < x1; x++) vt[x] = 0;
        }
        for (uint32_t t = 0; t < nt; t++) {
            if (!TILE_ON(d, t)) continue;
            for (uint32_t x = TILE * t, x1 = x + TILE < W ? x + TILE : W; x < x1; x++) {
                uint32_t v = (uint32_t)((ho[x] + 8192) >> 14);
                v = (v * a + 2048) >> 12;
                out[x] = (uint16_t)v;
                if (v >= H_MIN) {
                    if (x < s->bxl[o]) s->bxl[o] = x;
                    if (x > s->bxr[o]) s->bxr[o] = x;
                }
            }
        }
    }
}

/* 3. Full-resolution rows y0 <= y < y1 between the centres of cell rows b - 1 and b (o0, o1: slots of their blurred
 * rows; m0, m1: ring slots of their least codes; o0 == o1 above the first / below the last centre), only in the
 * groups next to cells with a halo. */
static void addback_rows(scr_t *s, const frame_t *f, uint32_t y0, uint32_t y1, uint32_t o0, uint32_t o1,
                         uint32_t m0, uint32_t m1) {
    const hp_t *p = &s->p;
    uint32_t W = s->W;
    uint32_t cl = s->bxl[o0] < s->bxl[o1] ? s->bxl[o0] : s->bxl[o1];
    uint32_t cr = s->bxl[o0] <= s->bxr[o0] ? s->bxr[o0] : 0;
    if (s->bxl[o1] <= s->bxr[o1] && s->bxr[o1] > cr) cr = s->bxr[o1];
    if (cl > cr || y0 >= y1) return;
    uint32_t gl = cl ? cl - 1 : 0, gr = cr + 1 < W ? cr + 1 : W - 1;   /* groups whose spans touch [cl, cr] */
    const uint8_t *mc0 = s->mc + m0 * W, *mc1 = s->mc + m1 * W;
    uint32_t nact = 0;
    for (uint32_t sp = gl; sp <= gr + 1; sp++) {
        uint32_t j0 = sp ? sp - 1 : 0, j1 = sp < W ? sp : W - 1;
        uint32_t mcm = mc0[j0];
        if (mc0[j1] < mcm) mcm = mc0[j1];
        if (mc1[j0] < mcm) mcm = mc1[j0];
        if (mc1[j1] < mcm) mcm = mc1[j1];
        int act = 0;
        for (int c = 0; c < 3 && !act; c++) {
            const uint16_t *b0 = s->b[c] + o0 * W, *b1 = s->b[c] + o1 * W;
            uint32_t bm = b0[j0];
            if (b0[j1] > bm) bm = b0[j1];
            if (b1[j0] > bm) bm = b1[j0];
            if (b1[j1] > bm) bm = b1[j1];
            act = p->step[c][mcm] != NO_STEP && bm >= (uint32_t)p->sa[c][mcm] + p->step[c][mcm];
        }
        s->span[sp] = (uint8_t)act;
        nact += (uint32_t)act;
    }
    if (!nact) return;
    uint32_t ng = 0;
    for (uint32_t g = gl; g <= gr; g++)
        if (s->span[g] | s->span[g + 1]) s->glist[ng++] = (uint16_t)g;
    /* The halo of pair j of group g at row y: Q8 horizontal interpolation on the two cell rows (weights: the pair
     * centres 1, 3, 5, 7 px into the group, cell centres 3.5 px into theirs; pairs 0, 1 between cells g - 1, g, pairs
     * 2, 3 between g, g + 1), then the vertical one in Q16, stepped one row at a time by hx_group(): row centre
     * y + 0.5, cell-row centres 8 b + 3.5, weight of the lower row (y - 3) mod 8 eighths (0 at the frame's edges). */
    int32_t wy0 = o0 == o1 ? 0 : (int32_t)((y0 - 3) & 7) << 5, dw = o0 == o1 ? 0 : 32;
    for (uint32_t q = 0; q < ng; q++) {
        uint32_t g = s->glist[q], jl = g ? g - 1 : 0, jr = g + 1 < W ? g + 1 : W - 1;
        int32_t *hp = s->hp + 24 * q;
        for (int c = 0; c < 3; c++) {
            const uint16_t *r0 = s->b[c] + o0 * W, *r1 = s->b[c] + o1 * W;
            int32_t a0 = r0[jl], m0 = r0[g], z0 = r0[jr], a1 = r1[jl], m1 = r1[g], z1 = r1[jr];
            int32_t P0[4] = { 256 * a0 + 176 * (m0 - a0), 256 * a0 + 240 * (m0 - a0), 256 * m0 + 48 * (z0 - m0),
                              256 * m0 + 112 * (z0 - m0) };
            int32_t P1[4] = { 256 * a1 + 176 * (m1 - a1), 256 * a1 + 240 * (m1 - a1), 256 * m1 + 48 * (z1 - m1),
                              256 * m1 + 112 * (z1 - m1) };
            for (int j = 0; j < 4; j++) {
                int32_t inc = (P1[j] - P0[j]) * dw;
                hp[4 * c + j] = 256 * P0[j] + (P1[j] - P0[j]) * wy0 + 32768 - inc;   /* hx_group adds inc first */
                hp[12 + 4 * c + j] = inc;
            }
        }
    }
    for (uint32_t y = y0; y < y1; y++) {
        s->rows++;
        uint32_t *yp = (uint32_t *)(f->y + y * f->py);
        uint32_t *bp = (uint32_t *)(f->cb + y * f->pc), *rp = (uint32_t *)(f->cr + y * f->pc);
        uint32_t words = 0;
        for (uint32_t q = 0; q < ng; q++) {
            uint32_t g = s->glist[q];
            s->kern.hp = s->hp + 24 * q;
            words += hx_group(&s->kern, yp + 2 * g, bp + g, rp + g);
        }
        s->groups += ng;
        s->words += words;
    }
}

/* 0. Cost estimate, before anything is written: the first sample row of every cell row through the pre-test, the
 * groups that pass decoded for a source; the source cells, dilated by rmax (the blur's reach), bound the cells with
 * a halo. Returns 0 if the pass fits HX_BUDGET (stops counting as soon as it cannot), else 1; *dec, *nh: decoded
 * groups, halo cells. Costs from the emulator's instruction counts (tests/emu, halation timing). */
static uint32_t over_budget(scr_t *s, const frame_t *f, uint32_t *dec_out, uint32_t *nh_out) {
    const hp_t *p = &s->p;
    uint32_t W = s->W, H = s->H, R = p->rmax, ww = (W + 31) >> 5, dec = 0, src = 0, nh = 0, all = p->all;
    uint32_t *em = s->em, last = W & 31 ? (1u << (W & 31)) - 1 : ~0u;
    *dec_out = *nh_out = 0;
    for (uint32_t i = 0; i < H; i++) {
        uint32_t *row = em + i * ww;
        for (uint32_t w = 0; w < ww; w++) row[w] = 0;
        const uint32_t *yp = (const uint32_t *)(f->y + CELL * i * f->py);
        const uint32_t *bp = (const uint32_t *)(f->cb + CELL * i * f->pc);
        const uint32_t *rp = (const uint32_t *)(f->cr + CELL * i * f->pc);
        for (uint32_t g = all ? 0 : scan(yp, bp, rp, 0, W, p); g < W;
             g = all ? g + 1 : scan(yp, bp, rp, g + 1, W, p)) {
            dec++;
            uint32_t y01 = yp[2 * g], y23 = yp[2 * g + 1], cbw = bp[g], crw = rp[g];
            for (int j = 0; j < 4; j++) {
                int32_t yy = (int32_t)(((j < 2 ? y01 : y23) >> ((j & 1) ? 8 : 24)) & 0xFF);
                int32_t cb = (int32_t)((cbw >> (24 - 8 * j)) & 0xFF) - 128;
                int32_t cr = (int32_t)((crw >> (24 - 8 * j)) & 0xFF) - 128;
                int32_t r = clamp255((p->m[0] * yy + p->m[1] * cb + p->m[2] * cr + 2048) >> 12);
                int32_t gg = clamp255((p->m[3] * yy + p->m[4] * cb + p->m[5] * cr + 2048) >> 12);
                int32_t b = clamp255((p->m[6] * yy + p->m[7] * cb + p->m[8] * cr + 2048) >> 12);
                if (p->src[0][r] | p->src[1][gg] | p->src[2][b]) {
                    row[g >> 5] |= 1u << (g & 31);
                    src++;
                    break;
                }
            }
        }
        /* over already: decoding, and the source cells themselves (a lower bound of the cells with a halo) */
        if (dec > (HX_BUDGET - EST_FIX) / EST_DEC || src > (HX_BUDGET - EST_FIX - dec * EST_DEC) / EST_CELL) {
            *dec_out = dec;
            return 1;
        }
        for (uint32_t n = 0; n < R; n++) {               /* horizontal dilation */
            uint32_t prev = 0;
            for (uint32_t w = 0; w < ww; w++) {
                uint32_t cur = row[w], next = w + 1 < ww ? row[w + 1] : 0;
                row[w] = cur | cur << 1 | cur >> 1 | prev >> 31 | next << 31;
                prev = cur;
            }
            row[ww - 1] &= last;
        }
    }
    uint32_t left = HX_BUDGET - EST_FIX - dec * EST_DEC;
    for (uint32_t i = 0; i < H; i++) {                  /* vertical dilation, counted */
        uint32_t i0 = i >= R ? i - R : 0, i1 = i + R < H ? i + R : H - 1;
        for (uint32_t w = 0; w < ww; w++) {
            uint32_t v = 0;
            for (uint32_t k = i0; k <= i1; k++) v |= em[k * ww + w];
            v = v - ((v >> 1) & 0x55555555u);
            v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
            nh += (((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
        }
        if (nh > left / EST_CELL) {
            *dec_out = dec;
            return 1;
        }
    }
    *dec_out = dec;
    *nh_out = nh;
    return 0;
}

static void pass(scr_t *s, const frame_t *f) {
    uint32_t H = s->H, R = s->p.rmax;
    for (int c = 0; c < 3; c++) {
        s->kern.t[c] = s->p.t[c];
        s->kern.ts[c] = s->p.ts[c];
    }
    for (int i = 0; i < 9; i++) {
        s->kern.m[i] = s->p.m[i];
        s->kern.c[i] = s->p.c[i];
    }
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
        uint32_t t0 = GR2_FN_SYSTEM_GETTIME(), dec, nh;
        if (over_budget(s, &f, &dec, &nh)) st = HX_BUSY;
        else pass(s, &f);
        r->t_pass = GR2_FN_SYSTEM_GETTIME() - t0;
        r->dec = dec;
        r->nh = nh;
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
    r->cells = r->rows = r->groups = r->words = r->dec = r->nh = 0;
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
        ERR_HEX2("halation: frame left untouched (status 6 buffer 7 table 8 chroma 9 size 10 outside 11 align "
                 "12 scratch busy 13 scratch size, ctx)", c.status, c.ctx);
    if (c.shot > g_logged_shot + 1) DBG_HEX("halation: shots not logged (overwritten)", c.shot - g_logged_shot - 1);
    g_logged_shot = c.shot;
    DBG_HEX2("halation: shot (n, status 0 done 1 tables 2 mode 3 RAW/DNG 4 interval/multi 5 over budget)", c.shot,
             c.status);
    DBG_HEX2("halation: (caller, ctx +0x408 | +0x357 << 8 | +0x008 << 16)", c.site, c.mode);
    DBG_HEX2("halation: (ctx +0x1E0 RAW, +0x258 w | h << 16)", c.raw, c.wh);
    DBG_HEX2("halation: (Y plane, scratch)", c.y, c.scratch);
    DBG_HEX2("halation: ms (stock step, pass)", c.t_stock, c.t_pass);
    DBG_HEX2("halation: (source cells, rows with a halo)", c.cells, c.rows);
    DBG_HEX2("halation: (8-px groups, words written)", c.groups, c.words);
    DBG_HEX2("halation: estimate (groups decoded, cells with a halo)", c.dec, c.nh);
}
