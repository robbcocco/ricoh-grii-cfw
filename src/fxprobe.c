/* H1 halation probe (finding 37 §7), debug builds only: DEV "Halation probe" (src/info.c).
 *
 * The still effect step FUN_a02AF6CE(ctx) runs on the developed full-resolution YCbCr 4:2:2 frame before the
 * review image and the JPEG (include/gr2_hw.h GR2_FX_*; section 8, loaded once at boot and never reloaded, so
 * the BLs can be patched at run time). Arming verifies that both call sites still hold the stock BL, fills the
 * stage-1 veneer's target word (0xA001E6CE, same low 12 bits as the stock target) and retargets both BLs, one
 * aligned halfword each; disarming restores them. A stale I-cache line can only hold a complete old BL: then the
 * stock step (armed) or this hook as a pass-through (disarmed) runs, both harmless.
 *
 * The hook runs in the still-processing task: unknown stack, no file I/O, no logging, no allocation. It calls the
 * stock step first, then, while armed, fills one record (seqlock: odd while it writes) that the CFW task logs:
 *   - the frame-context fields, the YCC buffer (handle "BUFF" -> address; size from the stock type table) and
 *     the plane addresses as the stock step computes them (GR2_FN_YCC_PLANES with -1, 3, as stock);
 *   - System_GetTime ms of the stock step, of a full read of the Y frame (w x h from ctx+0x258) through the
 *     cached address and then through the uncached alias (word sums: different sums = stale cache lines), and
 *     of the stamp;
 *   - the stamp: a square of SQUARE x SQUARE pixels (smaller frames: clipped) at the frame centre, Cr + CR_LIFT
 *     (saturating), Y and Cb unchanged. Cr is read through the uncached alias (engine-written data) and written
 *     through the cached address (write-through: DRAM at once, for the JPEG engine).
 * Every geometry value comes from ctx; nothing is written unless the frame fits its pitches and rows and every
 * plane's frame rectangle lies inside the YCC buffer [address, address + size). */
#include <stdint.h>
#include "boot.h"
#include "cfw.h"
#include "cfw_log.h"
#include "fxprobe.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"

#if CFW_DEBUG

#define SQUARE   512u                  /* side in pixels */
#define CR_LIFT  64u                   /* added to Cr: red tint */
#define DRAM(a)  (((a) >> 29) == 5u)   /* 0xA0000000-0xBFFFFFFF: cached DRAM with an uncached alias */

enum { FX_OK, FX_NO_BUFFER, FX_NO_TABLE, FX_NO_CHROMA, FX_BAD_SIZE, FX_OUTSIDE };

/* All words, so the CFW task can copy it word by word. */
typedef struct {
    uint32_t seq;                      /* odd while the hook writes */
    uint32_t shot;                     /* hook calls since arming */
    uint32_t site, ctx;                /* return address into the stock caller; frame context */
    uint32_t mode;                     /* ctx bytes +0x408 | +0x357 << 8 | +0x008 << 16 | +0x304 << 24 */
    uint32_t raw, ycc, ycc2, planes;   /* +0x1E0, +0x1F0, +0x1F8, +0x218 */
    uint32_t cb_off, cr_off;           /* +0x21C, +0x220 */
    uint32_t pitch_y, pitch_c, rows;   /* +0x224, +0x228, +0x22C */
    uint32_t y_top, y_left, c_top, c_left, y_row, c_row;   /* +0x230, +0x234, +0x238, +0x23C, +0x240, +0x244 */
    uint32_t wh;                       /* +0x258: w | h << 16 */
    uint32_t size3e4, x4b8, x4bc, x4c0;   /* +0x3E4, +0x4B8, +0x4BC, +0x4C0 (sizes / centre, finding 37 §3.1) */
    uint32_t buf, buf_size, y, cb, cr;
    uint32_t status;                   /* FX_* */
    uint32_t sq_xy, sq_wh;             /* square x0 | y0 << 16, w | h << 16 (0: none) */
    uint32_t t_stock, t_cached, t_uncached, t_stamp;   /* ms */
    uint32_t bytes, sum_cached, sum_uncached;          /* Y read: 0 bytes = not timed */
} fx_rec_t;

static volatile fx_rec_t g_rec;
static volatile uint32_t g_armed, g_shots;
static uint32_t g_veneer;
static volatile uint32_t *g_target;
static uint32_t g_logged_seq, g_logged_shot;

void fxprobe_init(const cfw_boot_t *boot) {
    g_veneer = boot->veneer_fx;
    g_target = boot->fx_target;
}

int fxprobe_armed(void) {
    return g_armed != 0;
}

uint32_t fxprobe_shots(void) {
    return g_shots;
}

static uint32_t bl_words(uint32_t site) {
    return (uint32_t)*(const volatile uint16_t *)site << 16 | *(const volatile uint16_t *)(site + 2);
}

int fxprobe_set(int on) {
    if (!g_veneer || !g_target) {
        ERR("fx: no effect veneer in stage 1, not armed");
        return -1;
    }
    if (on && !g_armed) {
        if (!cfw_bl_calls(GR2_FX_SITE_A, GR2_FX_STEP) || !cfw_bl_calls(GR2_FX_SITE_B, GR2_FX_STEP)) {
            ERR_HEX2("fx: call sites not stock, not armed (A, B)", bl_words(GR2_FX_SITE_A), bl_words(GR2_FX_SITE_B));
            return -1;
        }
        *g_target = (uint32_t)fxprobe_hook;   /* before any BL can reach the veneer */
        g_shots = 0;
        g_armed = 1;
        if (cfw_bl_retarget(GR2_FX_SITE_A, GR2_FX_STEP, g_veneer) != 0) {
            g_armed = 0;
            return -1;
        }
        if (cfw_bl_retarget(GR2_FX_SITE_B, GR2_FX_STEP, g_veneer) != 0) {
            g_armed = 0;
            (void)cfw_bl_retarget(GR2_FX_SITE_A, g_veneer, GR2_FX_STEP);
            return -1;
        }
        DBG("fx: armed");
    } else if (!on && g_armed) {
        g_armed = 0;                   /* a hook already running, or reached via a stale line, passes through */
        int rc = 0;
        for (int k = 0; k < 2; k++) {
            uint32_t site = k ? GR2_FX_SITE_B : GR2_FX_SITE_A;
            if (cfw_bl_calls(site, GR2_FX_STEP)) continue;
            rc |= cfw_bl_retarget(site, g_veneer, GR2_FX_STEP);
        }
        DBG("fx: disarmed");
        return rc;
    }
    return 0;
}

/* Sum of `rows` rows of `words` 32-bit words, `pitch` bytes apart (the read is what is timed). */
static uint32_t sum_plane(uint32_t a, uint32_t pitch, uint32_t words, uint32_t rows) {
    uint32_t s = 0;
    for (uint32_t r = 0; r < rows; r++, a += pitch) {
        const volatile uint32_t *p = (const volatile uint32_t *)a;
        uint32_t i = 0;
        for (; i + 4 <= words; i += 4) s += p[i] + p[i + 1] + p[i + 2] + p[i + 3];
        for (; i < words; i++) s += p[i];
    }
    return s;
}

/* `rows` rows of `width` bytes from `a`, `pitch` apart, lie inside [buf, buf + size). Values are 16-bit bounded
 * by the caller, so nothing overflows. */
static int inside(uint32_t a, uint32_t pitch, uint32_t width, uint32_t rows, uint32_t buf, uint32_t size) {
    if (a < buf || a - buf > size || !width || !rows) return 0;
    return (rows - 1) * pitch + width <= size - (a - buf);
}

#define CTX32(o) (*(const volatile uint32_t *)(ctx + (o)))
#define CTX8(o)  (*(const volatile uint8_t *)(ctx + (o)))

static uint32_t check(volatile fx_rec_t *r, uint32_t ctx) {
    uint32_t h = r->ycc;
    if (!h || (h & 3) || !DRAM(h) || *(const volatile uint32_t *)h != GR2_IMGBUF_MAGIC) return FX_NO_BUFFER;
    uint32_t buf = *(const volatile uint32_t *)(h + 4);
    const volatile uint8_t *type = (const volatile uint8_t *)(GR2_IMGBUF_TABLE + GR2_IMGBUF_YCC * GR2_IMGBUF_ENTRY);
    if (type[0] != GR2_IMGBUF_YCC) return FX_NO_TABLE;
    uint32_t size = *(const volatile uint32_t *)(type + 4);
    r->buf = buf;
    r->buf_size = size;
    if (!DRAM(buf) || !DRAM(buf + size - 1) || buf + size < buf) return FX_NO_BUFFER;
    uint32_t y = buf, cb = 0, cr = 0;
    GR2_FN_YCC_PLANES(&y, &cb, &cr, ctx, -1, 3);
    r->y = y;
    r->cb = cb;
    r->cr = cr;
    if (!cb || !cr) return FX_NO_CHROMA;
    uint32_t w = r->wh & 0xFFFFu, hh = r->wh >> 16;
    if (!w || !hh || r->pitch_y > 0xFFFFu || r->pitch_c > 0xFFFFu || r->rows > 0xFFFFu || w > r->pitch_y ||
        (w >> 1) > r->pitch_c || hh > r->rows)
        return FX_BAD_SIZE;
    if (!inside(y, r->pitch_y, w, hh, buf, size) || !inside(cb, r->pitch_c, w >> 1, hh, buf, size) ||
        !inside(cr, r->pitch_c, w >> 1, hh, buf, size))
        return FX_OUTSIDE;
    return FX_OK;
}

/* Cr + CR_LIFT in a square at the frame centre; check() has put the whole Cr frame inside the buffer. */
static void stamp(volatile fx_rec_t *r) {
    uint32_t w = r->wh & 0xFFFFu, hh = r->wh >> 16, pitch = r->pitch_c;
    uint32_t sx = w < SQUARE ? w & ~1u : SQUARE, sy = hh < SQUARE ? hh : SQUARE;
    uint32_t x0 = ((w - sx) >> 1) & ~1u, y0 = (hh - sy) >> 1;
    r->sq_xy = x0 | y0 << 16;
    r->sq_wh = sx | sy << 16;
    uint32_t a = r->cr + y0 * pitch + (x0 >> 1);
    for (uint32_t row = 0; row < sy; row++, a += pitch) {
        volatile uint8_t *c = (volatile uint8_t *)a;
        const volatile uint8_t *u = (const volatile uint8_t *)GR2_UNCACHED(a);
        for (uint32_t i = 0; i < (sx >> 1); i++) {
            uint32_t v = u[i];
            c[i] = (uint8_t)(v > 255u - CR_LIFT ? 255u : v + CR_LIFT);
        }
    }
}

void fxprobe_hook(uint32_t ctx) {
    uint32_t site = (uint32_t)__builtin_return_address(0);
    uint32_t t0 = GR2_FN_SYSTEM_GETTIME();
    ((void (*)(uint32_t))(GR2_FX_STEP | 1))(ctx);
    if (!g_armed) return;
    volatile fx_rec_t *r = &g_rec;
    uint32_t seq = r->seq;
    r->seq = seq | 1;
    r->t_stock = GR2_FN_SYSTEM_GETTIME() - t0;
    r->shot = ++g_shots;
    r->site = site;
    r->ctx = ctx;
    r->mode = CTX8(0x408) | (uint32_t)CTX8(0x357) << 8 | (uint32_t)CTX8(0x008) << 16 | (uint32_t)CTX8(0x304) << 24;
    r->raw = CTX32(0x1E0);
    r->ycc = CTX32(0x1F0);
    r->ycc2 = CTX32(0x1F8);
    r->planes = CTX32(0x218);
    r->cb_off = CTX32(0x21C);
    r->cr_off = CTX32(0x220);
    r->pitch_y = CTX32(0x224);
    r->pitch_c = CTX32(0x228);
    r->rows = CTX32(0x22C);
    r->y_top = CTX32(0x230);
    r->y_left = CTX32(0x234);
    r->c_top = CTX32(0x238);
    r->c_left = CTX32(0x23C);
    r->y_row = CTX32(0x240);
    r->c_row = CTX32(0x244);
    r->wh = CTX32(0x258);
    r->size3e4 = CTX32(0x3E4);
    r->x4b8 = CTX32(0x4B8);
    r->x4bc = CTX32(0x4BC);
    r->x4c0 = CTX32(0x4C0);
    r->buf = r->buf_size = r->y = r->cb = r->cr = 0;
    r->sq_xy = r->sq_wh = 0;
    r->t_cached = r->t_uncached = r->t_stamp = 0;
    r->bytes = r->sum_cached = r->sum_uncached = 0;
    r->status = check(r, ctx);
    if (r->status == FX_OK) {
        uint32_t w = r->wh & 0xFFFFu, hh = r->wh >> 16;
        if (!(r->y & 3) && !(r->pitch_y & 3)) {
            uint32_t t1 = GR2_FN_SYSTEM_GETTIME();
            r->sum_cached = sum_plane(r->y, r->pitch_y, w >> 2, hh);
            uint32_t t2 = GR2_FN_SYSTEM_GETTIME();
            r->sum_uncached = sum_plane(GR2_UNCACHED(r->y), r->pitch_y, w >> 2, hh);
            uint32_t t3 = GR2_FN_SYSTEM_GETTIME();
            r->t_cached = t2 - t1;
            r->t_uncached = t3 - t2;
            r->bytes = (w >> 2) * 4 * hh;
        }
        uint32_t t4 = GR2_FN_SYSTEM_GETTIME();
        stamp(r);
        r->t_stamp = GR2_FN_SYSTEM_GETTIME() - t4;
    }
    r->seq = (seq | 1) + 1;
}

static uint32_t kb_per_s(uint32_t bytes, uint32_t ms) {
    return ms ? udiv32(bytes, ms) : 0;   /* bytes per ms = kB/s; 0: below 1 ms */
}

void fxprobe_poll(void) {
    uint32_t seq = g_rec.seq;
    if ((seq & 1) || seq == g_logged_seq) return;
    fx_rec_t c;
    const volatile uint32_t *src = (const volatile uint32_t *)&g_rec;
    uint32_t *dst = (uint32_t *)&c;
    for (uint32_t i = 0; i < sizeof(c) / 4; i++) dst[i] = src[i];
    if (g_rec.seq != seq) return;      /* a newer shot was being written: next poll */
    g_logged_seq = seq;
    if (c.shot > g_logged_shot + 1) DBG_HEX("fx: shots not logged (overwritten)", c.shot - g_logged_shot - 1);
    g_logged_shot = c.shot;
    DBG_HEX2("fx: shot (n, caller)", c.shot, c.site);
    DBG_HEX2("fx: ctx (address, +0x408 mode | +0x357 << 8 | +0x008 << 16 | +0x304 << 24)", c.ctx, c.mode);
    DBG_HEX2("fx: handles (+0x1E0 RAW, +0x1F0 YCC)", c.raw, c.ycc);
    DBG_HEX2("fx: (+0x1F8 2nd YCC, +0x218 planes)", c.ycc2, c.planes);
    DBG_HEX2("fx: Cb/Cr offsets (+0x21C, +0x220)", c.cb_off, c.cr_off);
    DBG_HEX2("fx: pitches (+0x224 Y, +0x228 C)", c.pitch_y, c.pitch_c);
    DBG_HEX2("fx: (+0x22C rows, +0x258 w | h << 16)", c.rows, c.wh);
    DBG_HEX2("fx: Y margins (+0x230 top, +0x234 left)", c.y_top, c.y_left);
    DBG_HEX2("fx: C margins (+0x238 top, +0x23C left)", c.c_top, c.c_left);
    DBG_HEX2("fx: row offsets (+0x240 Y, +0x244 C)", c.y_row, c.c_row);
    DBG_HEX2("fx: (+0x3E4, +0x4C0)", c.size3e4, c.x4c0);
    DBG_HEX2("fx: (+0x4B8, +0x4BC)", c.x4b8, c.x4bc);
    DBG_HEX2("fx: YCC buffer (address, size)", c.buf, c.buf_size);
    DBG_HEX2("fx: planes (Y, Cb)", c.y, c.cb);
    DBG_HEX2("fx: (Cr plane, status 0 ok 1 buffer 2 table 3 chroma 4 size 5 outside)", c.cr, c.status);
    DBG_HEX2("fx: square (x0 | y0 << 16, w | h << 16)", c.sq_xy, c.sq_wh);
    DBG_HEX2("fx: ms (stock step, stamp)", c.t_stock, c.t_stamp);
    DBG_HEX2("fx: Y read (bytes, ms cached | ms uncached << 16)", c.bytes, c.t_cached | c.t_uncached << 16);
    DBG_HEX2("fx: Y read kB/s (cached, uncached)", kb_per_s(c.bytes, c.t_cached), kb_per_s(c.bytes, c.t_uncached));
    DBG_HEX2("fx: Y sums (cached, uncached)", c.sum_cached, c.sum_uncached);
}

#endif /* CFW_DEBUG */
