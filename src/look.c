/* Recipe looks in IQ RAM (finding 20 §B). CFW task only.
 *
 * The firmware reads every IQ record as section_pointer + index * size, and
 * re-reads the section pointer at each fetch. On first use the CFW copies the
 * camera-matrix, hue-sector, tone-curve and RGB->YCbCr sections into heap
 * buffers with room for its own records after the stock ones, and publishes
 * each copy with one aligned pointer store (identical contents: nothing
 * visible changes). A look is then shown by writing the CFW records and
 * pointing only the list entries of one IQ mode (Standard, Vivid, Setting1 or
 * Setting2) at them; every other mode keeps the stock records. Stills read the
 * lists at each capture; live view is told to reprogram through its change
 * cache (finding 20 §A). All writes hold the ISP mutex. */
#include <stdint.h>
#include "look.h"
#include "cfw_log.h"
#include "gr2_hw.h"

#include <stddef.h>
void *memcpy(void *dst, const void *src, size_t n);   /* src/mem.c */

#define SECT(s)       (*(volatile uint32_t *)(g_iq + 0x24 + 4 * (s)))
#define DIR(d)        (*(volatile uint16_t *)(g_iq + 0x90 + 2 * (d)))
#define U16(a)        (*(volatile uint16_t *)(a))
#define NONE          0xFFFF
#define N_MODES       4            /* IQ modes 0..3: Standard, Vivid, Setting1, Setting2 */
#define IQ_TYPE_STILL 3            /* lookup key k14: 0 live view, 3 stills */

/* Stage table: IQ section, lookup directory, record size, stock records, CFW records. */
enum { ST_MAT, ST_HUE, ST_TONE, ST_YCC, N_STAGES };
static const struct { uint8_t sect, dir; uint16_t size; uint8_t n, extra, flag; } STAGE[N_STAGES] = {
    { 0x08, 0x09, 0x18,   11, 1,  LOOK_MATRIX },
    { 0x09, 0x0A, 0x1A8,  6,  1,  LOOK_HUE },
    { 0x0B, 0x0C, 0x1400, 30, 3,  LOOK_TONE },
    { 0x0C, 0x0D, 0x14,   34, 18, LOOK_POST },
};
#define YCC_CLASSES 18   /* 9 saturation classes x {sRGB, AdobeRGB} */
static const uint16_t TONE_SECONDARY[6] = { NONE, 1, 2, NONE, 1, 2 };   /* per contrast class */

static uint32_t g_iq;
static int g_state;                    /* 0 not installed, 1 installed, -1 unavailable */
static uint8_t g_avail;                /* LOOK_* stages installed */
static uint8_t *g_copy[N_STAGES];

int look_mode(uint32_t cm) {
    return cm == 0 ? 0 : cm == 1 ? 1 : cm == 3 ? 2 : cm == 4 ? 3 : -1;
}

int look_tone_at(const uint8_t *k, int x) {
    int i = x >> 3, f = x & 7;
    if (i >= LOOK_KNOTS - 1) return k[LOOK_KNOTS - 1];
    return k[i] + (((k[i + 1] - k[i]) * f) >> 3);
}

/* ---- list entries ---------------------------------------------------------------- */

static uint32_t list(int st) { return g_iq + DIR(STAGE[st].dir); }

/* Point IQ mode k5 at the CFW records (stages in `flags`) or back at stock. */
static void set_entries(int k5, uint8_t flags) {
    uint32_t a = list(ST_MAT);
    for (int j = 0; j < 3; j++) U16(a + 4 * (3 * k5 + j)) = (flags & LOOK_MATRIX) ? 11 : 0;
    U16(list(ST_HUE) + 4 * k5) = (flags & LOOK_HUE) ? 6 : 0;
    a = list(ST_TONE);
    for (int t = 0; t <= IQ_TYPE_STILL; t += IQ_TYPE_STILL)
        for (int c = 0; c < 6; c++) {
            uint32_t e = a + 4 * (c + 18 * k5 + 342 * t);
            int sec = TONE_SECONDARY[c];
            U16(e) = (flags & LOOK_TONE) ? 30 : 0;
            U16(e + 2) = (flags & LOOK_TONE) ? (sec == NONE ? NONE : 30 + sec) : sec;
        }
    a = list(ST_YCC);
    for (int r = 0; r < YCC_CLASSES; r++)
        U16(a + 2 * (r + 18 * k5)) = (uint16_t)((flags & LOOK_POST) ? 34 + r : r);
}

static int entries_stock(int k5) {
    uint32_t a = list(ST_MAT);
    for (int j = 0; j < 3; j++)
        if (U16(a + 4 * (3 * k5 + j)) != 0 || U16(a + 4 * (3 * k5 + j) + 2) != NONE) return 0;
    if (U16(list(ST_HUE) + 4 * k5) != 0 || U16(list(ST_HUE) + 4 * k5 + 2) != NONE) return 0;
    a = list(ST_TONE);
    for (int t = 0; t <= IQ_TYPE_STILL; t += IQ_TYPE_STILL)
        for (int c = 0; c < 6; c++) {
            uint32_t e = a + 4 * (c + 18 * k5 + 342 * t);
            if (U16(e) != 0 || U16(e + 2) != TONE_SECONDARY[c]) return 0;
        }
    a = list(ST_YCC);
    for (int r = 0; r < YCC_CLASSES; r++) if (U16(a + 2 * (r + 18 * k5)) != r) return 0;
    return 1;
}

/* ---- install --------------------------------------------------------------------- */

static int locate(void) {
    uint32_t ctx = GR2_IQ_CTX;
    if (ctx < 0xA0000000u || ctx >= 0xC0000000u) return 0;          /* ISP not set up yet */
    int bank = *(volatile int8_t *)(ctx + 0x2C);
    if (bank < 0 || bank > 1) return 0;
    uint32_t iq = *(volatile uint32_t *)(ctx + 0x158 + 4 * bank);
    const volatile uint8_t *h = (const volatile uint8_t *)iq;
    if (iq < 0xA0000000u || iq >= 0xC0000000u || h[0] != '0' || h[1] != '2' || h[2] != '0' ||
        h[3] != '0' || h[0x10] != 'M' || h[0x11] != '6' || h[0x12] != 'M' || h[0x13] != 'E') {
        ERR_HEX("look: IQ data not found, ctx", ctx);
        return -1;
    }
    g_iq = iq;
    for (int st = 0; st < N_STAGES; st++) {
        uint32_t s = STAGE[st].sect;
        if (SECT(s + 1) - SECT(s) != (uint32_t)STAGE[st].n * STAGE[st].size) {
            ERR_HEX("look: IQ section size not stock, section", s);
            return -1;
        }
    }
    for (int k = 0; k < N_MODES; k++)
        if (!entries_stock(k)) {
            ERR_HEX("look: IQ lists not stock, mode", (uint32_t)k);
            return -1;
        }
    return 1;
}

static void *isp_lock(void) {
    void *m = GR2_ISP_MUTEX;
    if (m && GR2_FN_MUTEX_LOCK(m, -1) == 0) return m;
    ERR("look: ISP mutex unavailable, live view updates late");
    return 0;
}

/* Live view compares its IQ lookups with a cache and reprograms only on a
 * change: flip the cached tone entry and EV so the next frame reprograms all
 * stages and reloads the curves (finding 20 §A). */
static void isp_unlock_refresh(void *m) {
    GR2_LV_CACHE_CURVE ^= 0xFFFF;
    GR2_LV_CACHE_EV ^= 0x80000000u;
    if (m) GR2_FN_MUTEX_UNLOCK(m);
}

static int install(void) {
    int rc = locate();
    if (rc <= 0) {
        if (rc < 0) g_state = -1;
        return rc;
    }
    for (int st = 0; st < N_STAGES; st++) {
        uint32_t stock = (uint32_t)STAGE[st].n * STAGE[st].size;
        uint8_t *c = GR2_FN_MEMORY_ALLOC(stock + (uint32_t)STAGE[st].extra * STAGE[st].size);
        if (!c || ((uint32_t)c & 3)) {
            ERR_HEX("look: Memory_Alloc failed, stage unavailable", STAGE[st].flag);
            continue;
        }
        memcpy(c, (const void *)SECT(STAGE[st].sect), stock);
        g_copy[st] = c;
        g_avail |= STAGE[st].flag;
    }
    void *m = isp_lock();
    for (int st = 0; st < N_STAGES; st++)
        if (g_copy[st]) SECT(STAGE[st].sect) = (uint32_t)g_copy[st];   /* same contents */
    if (m) GR2_FN_MUTEX_UNLOCK(m);
    g_state = 1;
    DBG_HEX2("look: IQ sections copied (iq, stages)", g_iq, g_avail);
    return 1;
}

/* ---- records --------------------------------------------------------------------- */

static int16_t clamp16(int32_t v, int32_t lim) {
    return (int16_t)(v < -lim ? -lim : v > lim ? lim : v);
}

/* out = a/128 x b (3x3, row-major), rounded and clamped. */
static void mul3(const int16_t *a, const volatile int16_t *b, int16_t *out, int32_t lim) {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            int32_t acc = 0;
            for (int k = 0; k < 3; k++) acc += (int32_t)a[3 * i + k] * b[3 * k + j];
            out[3 * i + j] = clamp16((acc + 64) >> 7, lim);
        }
}

static void write_records(const look_t *l, uint8_t flags) {
    if (flags & LOOK_MATRIX) {                       /* record 11 = rec 0 with M = matrix x stock */
        uint8_t *base = g_copy[ST_MAT], *rec = base + 11 * 0x18;
        int16_t m[9];
        memcpy(rec, base, 0x18);
        mul3(l->matrix, (const volatile int16_t *)(base + 4), m, 2047);
        memcpy(rec + 4, m, sizeof(m));
    }
    if (flags & LOOK_HUE) {                          /* record 6 = rec 0 with T_k = hue_k x T_k */
        uint8_t *base = g_copy[ST_HUE], *rec = base + 6 * 0x1A8;
        memcpy(rec, base, 0x1A8);
        for (int k = 0; k < LOOK_SECTORS; k++) {
            int16_t t[9];
            mul3(l->hue[k], (const volatile int16_t *)(base + 0x40 + 0x1E * k), t, 8191);
            memcpy(rec + 0x40 + 0x1E * k, t, sizeof(t));
        }
    }
    if (flags & LOOK_TONE) {                         /* sets 30..32 = film curve o stock sets 0..2 */
        uint8_t *base = g_copy[ST_TONE];
        for (int s = 0; s < 3; s++) {
            const uint8_t *src = base + s * 0x1400;
            uint8_t *dst = base + (30 + s) * 0x1400;
            for (int ch = 1; ch <= 3; ch++)
                for (int i = 0; i < 0x400; i++)
                    dst[ch * 0x400 + i] = (uint8_t)look_tone_at(l->tone[ch - 1], src[ch * 0x400 + i]);
            memcpy(dst, dst + 0x400, 0x400);         /* ch0 = R, as in every stock set */
            memcpy(dst + 0x1000, src + 0x1000, 0x400);   /* ch4 (Y-gain path) stays stock */
        }
    }
    if (flags & LOOK_POST) {                         /* records 34.. = stock class r x post */
        uint8_t *base = g_copy[ST_YCC];
        for (int r = 0; r < YCC_CLASSES; r++) {
            const volatile int16_t *c = (const volatile int16_t *)(base + r * 0x14);
            uint8_t *dst = base + (34 + r) * 0x14;
            int16_t m[9];
            for (int i = 0; i < 3; i++)              /* C' = C x P/128: P acts on the RGB input */
                for (int j = 0; j < 3; j++) {
                    int32_t acc = 0;
                    for (int k = 0; k < 3; k++) acc += (int32_t)c[3 * i + k] * l->post[3 * k + j];
                    m[3 * i + j] = clamp16((acc + 64) >> 7, 255);
                }
            memcpy(dst, m, sizeof(m));
            memcpy(dst + 18, base + r * 0x14 + 18, 2);
        }
    }
}

/* ---- apply ----------------------------------------------------------------------- */

int look_apply(const look_t *l, int k5) {
    int want = l && l->flags && k5 >= 0 && k5 < N_MODES;
    if (g_state == 0 && want) install();
    if (g_state <= 0) return want ? -1 : 0;
    uint8_t flags = want ? (uint8_t)(l->flags & g_avail) : 0;
    void *m = isp_lock();
    for (int k = 0; k < N_MODES; k++) set_entries(k, 0);
    if (flags) {
        write_records(l, flags);
        set_entries(k5, flags);
    }
    isp_unlock_refresh(m);
    return 0;
}
