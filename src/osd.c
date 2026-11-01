/* OSD access for the CFW overlay (static RE: finding 17).
 *
 * The LCD shows OSD layer 3 from one of two 8-bpp palette bitmaps (0x54600 B
 * each) that the APP task flips on every present. While the overlay is open:
 *   - the APP draw/present step is skipped via the draw-lock word 0xA077D58C;
 *   - we draw into the stock back buffer (unused while locked) and point
 *     layer 3 at it;
 * on close, layer 3 goes back to the stock front buffer and the lock is
 * released, so the stock UI repaints on its next message. No stock pixels
 * are modified while the stock frame is on screen.
 *
 * Geometry and format (verified on hardware 2026-10-30 from a dump of the
 * stock front buffer): Prop 0x254 = 0x01E00280 (640x480), scan-out pitch 640
 * bytes, 8-bit palette indices. Palette entries are 0x0AYYUUVV (4-bit alpha,
 * YCbCr), read with FUN_a0393990(idx). Buffers are at 0x20xxxxxx (uncached).
 *
 * Text: GFNT fonts from H:/cfw/fonts (4-bit coverage per pixel, see
 * scripts/make_fonts.py). Edge pixels are blended with the pixel underneath
 * in YCbCr and mapped to the nearest opaque palette entry; ramps are cached
 * per (background, foreground) pair. Without font files the ROM font is used. */
#include <stdint.h>
#include "cfw_log.h"
#include "gr2_hw.h"
#include "osd.h"

#define DRAWLOCK        (*(volatile uint32_t *)0xA077D58C)
#define GSTATE          ((volatile uint8_t *)0xA04070AC)    /* +0 OSD front index, +3 output */
#define BITMAPS         ((volatile uint32_t *)0xA077C418)   /* [0],[1] OSD double buffer */
#define PROPID_OSD_SIZE 0x254                              /* (h << 16) | w */
#define OSD_BUF_BYTES   0x54600                            /* per OSD bitmap */
#define FN_SET_LAYER    ((void (*)(int, int, uint32_t *))0xA00D717F)  /* (dev, layer, cfg[6]) */
#define FN_PALETTE_GET  ((uint32_t (*)(int))0xA0393991)    /* palette entry idx -> 0x0AYYUUVV */
/* FN_PALETTE_SET writes the shadow tables and the HIDDEN hardware bank only and
 * sets PAL_PENDING; the stock present (FUN_a0393bac) then flips the bank. Our
 * overlay holds the draw lock, so it commits itself (finding 17 §palette). */
#define FN_PALETTE_SET  ((void (*)(uint32_t, int))0xA03939EF)  /* (0x0AYYUUVV, idx) */
#define FN_OSD_STATE    ((uint32_t (*)(void))0xA00F5FE3)   /* bit 24 LCD / 26 video: shown bank */
#define FN_HDMI_STATE   ((uint32_t (*)(void))0xA0109863)   /* bit 25: shown bank */
#define FN_PAL_BANK     ((int (*)(int, int))0xA00F5F93)    /* (3, bank): show bank (LCD/video) */
#define FN_HDMI_PAL_BANK ((void (*)(int))0xA010982B)       /* (bank): show bank (HDMI) */
#define PAL_PENDING     (*(volatile uint32_t *)0xA04070C8) /* GSTATE + 0x1C */
#define BANK_SETTLE_MS  50                                 /* bank flip latches at vsync */
#define LAYER_OSD       3

#define ROM_FONT        0xA05BA9B8u     /* 1-bpp glyphs 0x20..0x7E, 32x38, 4 B/row, 0 bit = ink */
#define ROM_GLYPH_BYTES 0x98
#define ROM_ADVANCE     34
#define ROM_LINE        40

#define FONT_PATH       "H:/cfw/fonts/"
#define FONT_MAX_BYTES  0x10000
static const char *const FONT_FILE[FONT_COUNT] = {
    "inm13.fnt", "insb15.fnt", "inm18.fnt", "insb18.fnt", "bcsb22.fnt",
};

static uint8_t *g_buf;
static int g_dev;
static int g_w, g_h;
static const uint8_t *g_font[FONT_COUNT];
static uint32_t *g_pal;                       /* 256 palette entries, read at open */
static uint32_t g_user_saved;                 /* bit k: entry OSD_PAL_USER + k changed since open */

typedef struct { uint8_t bg, fg, used, pad; uint8_t ramp[16]; } ramp_t;
#define N_RAMPS 12
static ramp_t g_ramps[N_RAMPS];
static int g_ramp_next;

int osd_width(void) { return g_w; }
int osd_height(void) { return g_h; }

/* ---- fonts --------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static const uint8_t *load_font(const char *name) {
    char path[40], *p = path;
    for (const char *s = FONT_PATH; *s; s++) *p++ = *s;
    while (*name) *p++ = *name++;
    *p = 0;
    int fd = GR2_FN_FILE_OPEN(path, 0x00);
    if (fd == 0) return 0;
    uint8_t head[12];
    uint8_t *f = 0;
    if (GR2_FN_FILE_READ(fd, head, 12) == 12 && head[0] == 'G' && head[1] == 'F' && head[2] == 'N' &&
        head[3] == 'T' && head[4] == 1) {
        uint32_t size = be32(head + 8);
        if (size > 12u + 8u * head[7] && size <= FONT_MAX_BYTES) {
            f = GR2_FN_MEMORY_ALLOC(size);
            if (f) {
                for (int i = 0; i < 12; i++) f[i] = head[i];
                if (GR2_FN_FILE_READ(fd, f + 12, size - 12) != (int)(size - 12)) f = 0;   /* block leaks */
            }
        }
    }
    GR2_FN_FILE_CLOSE(fd);
    return f;
}

void osd_fonts_load(void) {
    for (int i = 0; i < FONT_COUNT; i++) {
        g_font[i] = load_font(FONT_FILE[i]);
        if (!g_font[i]) ERR_HEX("osd: font missing or invalid, using the ROM font (index)", (uint32_t)i);
    }
}

static const uint8_t *glyph(const uint8_t *f, uint8_t c) {
    int i;
    if (c >= 0x20 && c < 0x7F) i = c - 0x20;
    else if (c >= 0x80 && c < 0x80 + (f[7] - 95)) i = 95 + (c - 0x80);
    else i = '?' - 0x20;
    return f + 12 + 8 * i;
}

int osd_line_height(int font) {
    return g_font[font] ? g_font[font][5] : ROM_LINE;
}

int osd_text_width(int font, const char *s) {
    const uint8_t *f = g_font[font];
    int w = 0;
    for (; *s; s++) w += f ? glyph(f, (uint8_t)*s)[6] : ROM_ADVANCE;
    return w;
}

/* ---- palette blending ------------------------------------------------------------ */

static uint8_t nearest_yuv(int y, int u, int v) {
    int best = OSD_BLACK, best_d = 0x7FFFFFFF;
    for (int i = 1; i < 256; i++) {
        if (i >= OSD_PAL_USER && i < OSD_PAL_USER + OSD_PAL_USER_N) continue;   /* overlay-owned */
        uint32_t e = g_pal[i];
        if ((e >> 24) != 0x0F) continue;                 /* opaque entries only */
        int dy = (int)((e >> 16) & 0xFF) - y, du = (int)((e >> 8) & 0xFF) - u, dv = (int)(e & 0xFF) - v;
        int d = 2 * dy * dy + du * du + dv * dv;
        if (d < best_d) { best_d = d; best = i; }
    }
    return (uint8_t)best;
}

static uint32_t rgb_to_entry(uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    int y = (77 * r + 150 * g + 29 * b) >> 8;
    int u = 128 + ((((int)b - y) * 144) >> 8);
    int v = 128 + ((((int)r - y) * 183) >> 8);
    if (u < 0) u = 0; else if (u > 255) u = 255;
    if (v < 0) v = 0; else if (v > 255) v = 255;
    return ((uint32_t)(alpha & 0xF) << 24) | ((uint32_t)y << 16) | ((uint32_t)u << 8) | (uint32_t)v;
}

/* The bank FN_PALETTE_SET writes (the hidden one), as computed in 0xA03939EE. */
static int pal_hidden_bank(uint8_t out) {
    if (out >= 4 && out <= 6) return (FN_HDMI_STATE() >> 25) & 1 ? 0 : 1;
    uint32_t s = FN_OSD_STATE();
    if (out == 1) return (s >> 24) & 1 ? 0 : 1;
    if (out == 2 || out == 3) return (s >> 26) & 1 ? 0 : 1;
    return 0;
}

static uint32_t g_user_val[OSD_PAL_USER_N];   /* entry values shown (both banks) */
static uint32_t g_user_dirty;                 /* bit k: set in the hidden bank only */

uint8_t osd_pal_user(int k, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    if (!g_pal || !g_buf || k < 0 || k >= OSD_PAL_USER_N) return osd_nearest(r, g, b);
    uint32_t e = rgb_to_entry(r, g, b, alpha);
    int idx = OSD_PAL_USER + k;
    if (g_user_val[k] != e) {
        g_user_val[k] = e;
        g_user_saved |= 1u << k;
        g_user_dirty |= 1u << k;
        FN_PALETTE_SET(e, idx);
    }
    return (uint8_t)idx;
}

/* Show the hidden bank, then write the same entries into the other bank so
 * both hold them (a later stock flip changes nothing). */
void osd_pal_commit(void) {
    if (!g_user_dirty || !PAL_PENDING) return;
    uint8_t out = GSTATE[3];
    int bank = pal_hidden_bank(out);
    if (out >= 4 && out <= 6) FN_HDMI_PAL_BANK(bank);
    else FN_PAL_BANK(3, bank);
    PAL_PENDING = 0;
    GR2_FN_DELAY(BANK_SETTLE_MS);
    for (int k = 0; k < OSD_PAL_USER_N; k++)
        if (g_user_dirty & (1u << k)) FN_PALETTE_SET(g_user_val[k], OSD_PAL_USER + k);
    PAL_PENDING = 0;
    g_user_dirty = 0;
}

uint8_t osd_nearest(uint8_t r, uint8_t g, uint8_t b) {
    if (!g_pal) return OSD_WHITE;
    int y = (77 * r + 150 * g + 29 * b) >> 8;
    int u = 128 + ((((int)b - y) * 144) >> 8);
    int v = 128 + ((((int)r - y) * 183) >> 8);
    return nearest_yuv(y, u, v);
}

static const uint8_t *ramp(uint8_t bg, uint8_t fg) {
    for (int i = 0; i < N_RAMPS; i++)
        if (g_ramps[i].used && g_ramps[i].bg == bg && g_ramps[i].fg == fg) return g_ramps[i].ramp;
    ramp_t *r = &g_ramps[g_ramp_next];
    g_ramp_next = g_ramp_next == N_RAMPS - 1 ? 0 : g_ramp_next + 1;
    r->bg = bg; r->fg = fg; r->used = 1;
    r->ramp[0] = bg; r->ramp[15] = fg;
    uint32_t b = g_pal ? g_pal[bg] : 0, f = g_pal ? g_pal[fg] : 0;
    int by = (b >> 16) & 0xFF, bu = (b >> 8) & 0xFF, bv = b & 0xFF;
    int fy = (f >> 16) & 0xFF, fu = (f >> 8) & 0xFF, fv = f & 0xFF;
    /* Transparent / shadow / overlay-owned (values change per frame, ramps are cached
     * by index): threshold, a dark blend would halo. */
    if (!g_pal || (b >> 24) != 0x0F || (bg >= OSD_PAL_USER && bg < OSD_PAL_USER + OSD_PAL_USER_N)) {
        for (int k = 1; k < 15; k++) r->ramp[k] = k >= 7 ? fg : bg;   /* a dark blend would halo */
        return r->ramp;
    }
    for (int k = 1; k < 15; k++) {
        int t = k * 273;                                      /* k / 15 in 1/4096 */
        r->ramp[k] = nearest_yuv(by + (((fy - by) * t) >> 12), bu + (((fu - bu) * t) >> 12),
                                 bv + (((fv - bv) * t) >> 12));
    }
    return r->ramp;
}

/* ---- drawing ------------------------------------------------------------------------ */

static void set_layer(uint32_t addr) {
    uint32_t cfg[6] = { addr, 0, 0, 0, 0, 0 };
    FN_SET_LAYER(g_dev, LAYER_OSD, cfg);
}

static int output_device(void) {
    uint8_t out = GSTATE[3];
    if (out == 1) return 0;          /* LCD */
    if (out <= 3) return 2;          /* video out */
    return 1;                        /* HDMI */
}

int osd_open(void) {
    uint32_t size = GR2_FN_PROP_GET(PROPID_OSD_SIZE);
    g_w = (int)(size & 0xFFFF);
    g_h = (int)(size >> 16);
    if (g_w <= 0 || g_h <= 0 || g_w * g_h > OSD_BUF_BYTES) {
        ERR_HEX("osd: unexpected OSD size", size);
        return -1;
    }
    if (!g_pal) g_pal = GR2_FN_MEMORY_ALLOC(256 * 4);
    if (g_pal) for (int i = 0; i < 256; i++) g_pal[i] = FN_PALETTE_GET(i);
    for (int i = 0; i < N_RAMPS; i++) g_ramps[i].used = 0;
    for (int k = 0; k < OSD_PAL_USER_N; k++) g_user_val[k] = g_pal ? g_pal[OSD_PAL_USER + k] : 0;
    g_user_saved = g_user_dirty = 0;
    DRAWLOCK = 1;
    GR2_FN_DELAY(50);                /* let an in-flight async present finish */
    g_dev = output_device();
    g_buf = (uint8_t *)BITMAPS[GSTATE[0] ^ 1];
    osd_fill(0, 0, g_w, g_h, OSD_TRANSPARENT);
    set_layer((uint32_t)g_buf);
    return 0;
}

void osd_close(void) {
    /* Stock colours back before the stock buffer returns. The palette swap takes a
     * bank flip (~50 ms) while this buffer is still shown, so first make its pixels
     * that use the overlay-owned entries transparent: otherwise they flash in the
     * stock colours of those entries (green on the filmstrip, 2026-11-01). */
    if (g_user_saved) {
        uint8_t *p = g_buf, *end = g_buf + g_w * g_h;
        for (; p < end; p++)
            if (*p >= OSD_PAL_USER && *p < OSD_PAL_USER + OSD_PAL_USER_N) *p = OSD_TRANSPARENT;
    }
    for (int k = 0; k < OSD_PAL_USER_N; k++)
        if (g_user_saved & (1u << k)) {
            g_user_val[k] = g_pal[OSD_PAL_USER + k];
            g_user_dirty |= 1u << k;
            FN_PALETTE_SET(g_user_val[k], OSD_PAL_USER + k);
        }
    osd_pal_commit();
    g_user_saved = 0;
    set_layer(BITMAPS[GSTATE[0]]);
    DRAWLOCK = 0;
    g_buf = 0;
}

const uint8_t *osd_shown_layer(uint32_t pal[256]) {
    for (int i = 0; i < 256; i++) pal[i] = FN_PALETTE_GET(i);
    return (const uint8_t *)GR2_UNCACHED(BITMAPS[GSTATE[0]]);
}

void osd_fill(int x, int y, int w, int h, uint8_t color) {
    if (!g_buf) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    for (int row = 0; row < h; row++) {
        uint8_t *p = g_buf + (y + row) * g_w + x;
        for (int col = 0; col < w; col++) p[col] = color;
    }
}

void osd_frame(int x, int y, int w, int h, int t, uint8_t color) {
    osd_fill(x, y, w, t, color);
    osd_fill(x, y + h - t, w, t, color);
    osd_fill(x, y, t, h, color);
    osd_fill(x + w - t, y, t, h, color);
}

static void rom_char(int x, int y, char c, uint8_t fg) {
    if (c < 0x20 || c > 0x7E) c = '?';
    const volatile uint32_t *g = (const volatile uint32_t *)(ROM_FONT + (uint32_t)(c - 0x20) * ROM_GLYPH_BYTES);
    for (int row = 0; row < 38; row++) {
        int py = y + row;
        if (py < 0 || py >= g_h) continue;
        uint32_t bits = g[row];
        uint8_t *p = g_buf + py * g_w;
        for (int col = 0; col < 32; col++) {
            int px = x + col;
            if (px >= 0 && px < g_w && !((bits >> (31 - col)) & 1)) p[px] = fg;
        }
    }
}

static void draw_glyph(const uint8_t *f, const uint8_t *gl, int x, int y, uint8_t fg) {
    int w = gl[2], h = gl[3], xo = (int8_t)gl[4], yo = (int8_t)gl[5];
    const uint8_t *bm = f + 12 + 8 * f[7] + ((gl[0] << 8) | gl[1]);
    int rowbytes = (w + 1) >> 1;
    for (int row = 0; row < h; row++) {
        int py = y + yo + row;
        if (py < 0 || py >= g_h) continue;
        uint8_t *line = g_buf + py * g_w;
        for (int col = 0; col < w; col++) {
            int px = x + xo + col;
            if (px < 0 || px >= g_w) continue;
            uint8_t byte = bm[row * rowbytes + (col >> 1)];
            int a = (col & 1) ? (byte & 0x0F) : (byte >> 4);
            if (a == 0) continue;
            line[px] = a == 15 ? fg : ramp(line[px], fg)[a];
        }
    }
}

int osd_text(int font, int x, int y, const char *s, uint8_t fg, int xmax) {
    if (!g_buf) return x;
    const uint8_t *f = g_font[font];
    for (; *s; s++) {
        if (!f) {
            if (x + ROM_ADVANCE > xmax) break;
            rom_char(x, y, *s, fg);
            x += ROM_ADVANCE;
            continue;
        }
        const uint8_t *gl = glyph(f, (uint8_t)*s);
        if (x + gl[6] > xmax) break;
        draw_glyph(f, gl, x, y, fg);
        x += gl[6];
    }
    return x;
}
