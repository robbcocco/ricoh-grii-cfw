/* Persistent overlays over live view (finding 23, Tier A). TTY task only.
 *
 * In every shooting screen the camera shows its RGBA4444 "full" plane (display
 * layer 2) between live view and the OSD; it is transparent except for the
 * stock grid guide and effect masks. TTY stamps its pixels into the buffer
 * stock currently shows (bitmap 2 or 3, index G[1]) and stamps again after
 * every stock flip (stock copies its canvas over the whole buffer at a flip).
 * Nothing is re-pointed or enabled, and the overlay disappears by itself in
 * menus and playback, where stock turns the plane off. Pixels are only ever
 * overwritten, so stamping is idempotent.
 *
 * Frame lines: a translucent mask outside the chosen frame plus a white line,
 * inside the live-view image area of the current aspect ratio.
 * Zebras: cells of 4x4 pixels whose luma (sampled every 4th row) reaches the
 * threshold get animated stripes; cold cells are restored from the stock
 * canvas and the frame overlay. */
#include <stdint.h>
#include "overlay.h"
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"

#define GST           ((volatile uint8_t *)0xA04070AC)        /* +1 full-plane front index (2/3), +3 output (1 = LCD) */
#define GST_MASK      (*(volatile uint32_t *)0xA04070B8)      /* plane mask of the current screen: bit 1 = full plane */
#define BMP           ((volatile uint32_t *)0xA077C418)       /* bitmaps; [2], [3] = full-plane buffers, pitch 640 px */
#define RGB_CANVAS    (*(volatile uint32_t *)0xA043EA44)      /* stock RGB canvas (bitmap 6), pitch 720 px */
#define REC_RGB_REDRAW (*(volatile uint32_t *)0xA0424360)     /* stock rebuilds grid/masks and flips at its next REC draw */
#define SCREEN_CUR    (*(volatile uint8_t *)0xA0407201)
#define REC_STATE     (*(volatile uint8_t *)0xA0424358)
#define FN_LAYER_ON   ((int (*)(int, int))0xA00D7149)         /* (dev, layer) -> enabled; takes the Video lock */
#define PROPID_SYSTEM_MODE 0x000
#define SYSMODE_SHOOTING   6
#define SYSMODE_MOVIE      9

#define W          640
#define H          480
#define CV_PITCH   720
#define C_CLEAR    0x0000u      /* RGBA4444: R<<12 | G<<8 | B<<4 | A */
#define C_LINE     0xFFFCu      /* white, A 12 */
#define C_MASK     0x000Au      /* black, A 10 */
#define C_NONE     0x10000u     /* frame_px: no overlay pixel here */
#define FLIP_SETTLE_MS 40       /* stock's canvas -> front copy is asynchronous */
#define ZEBRA_POLLS 5           /* one zebra pass per 100 ms */

static const uint8_t ZEBRA_Y[4] = { 0, 235, 245, 252 };   /* LV luma thresholds (felt right on hardware) */
static const uint8_t FRAME_RATIO[FRAME_COUNT][2] = {       /* width : height */
    { 0, 0 }, { 65, 24 }, { 139, 112 }, { 5, 4 }, { 1, 1 }, { 16, 9 } };   /* 6x7: 69.5 x 56 mm gate */

static int g_frame, g_zebra, g_changed, g_drawn, g_test;
static uint8_t g_last = 0xFF;                              /* G[1] we stamped, 0xFF = none */
static int g_zpoll, g_phase;
static uint8_t g_zcell[H / 4][W / 4];                      /* 1 = stripes shown in that 4x4 cell */

typedef struct { int x, y, w, h; } rect_t;
static rect_t g_area, g_box;                               /* image area, frame inside it */

void overlay_set_frame(int frame) {
    if (frame < 0 || frame >= FRAME_COUNT || frame == g_frame) return;
    g_frame = frame;
    g_changed = 1;
}

void overlay_set_zebra(int level) {
    if (level < 0 || level > 3 || level == g_zebra) return;
    g_zebra = level;
    g_changed = 1;
}

static int fullplane_live(void) {
    uint32_t sm = GR2_FN_PROP_GET(PROPID_SYSTEM_MODE);
    return GST[3] == 1 && (GST_MASK & 2) && (sm == SYSMODE_SHOOTING || sm == SYSMODE_MOVIE) &&
           FN_LAYER_ON(0, 2);
}

static volatile uint16_t *front(void) {
    return (volatile uint16_t *)GR2_UNCACHED(BMP[GST[1]]);
}

static void geometry(void) {
    switch (GR2_LV_ASPECT) {
    case 0:  g_area = (rect_t){ 0, 60, 640, 360 }; break;
    case 2:  g_area = (rect_t){ 0, 28, 640, 424 }; break;
    case 3:  g_area = (rect_t){ 80, 0, 480, 480 }; break;
    default: g_area = (rect_t){ 0, 0, 640, 480 }; break;
    }
    g_box = g_area;
    if (!g_frame) return;
    uint32_t num = FRAME_RATIO[g_frame][0], den = FRAME_RATIO[g_frame][1];
    if ((uint32_t)g_area.w * den > (uint32_t)g_area.h * num) {   /* area wider than the frame */
        g_box.w = (int)udiv32((uint32_t)g_area.h * num, den) & ~1;
        g_box.x = g_area.x + ((g_area.w - g_box.w) >> 1);
    } else {
        g_box.h = (int)udiv32((uint32_t)g_area.w * den, num) & ~1;
        g_box.y = g_area.y + ((g_area.h - g_box.h) >> 1);
    }
}

static int inside(const rect_t *r, int x, int y) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* Frame overlay at one pixel: mask outside the frame, 2-px line on the edges
 * where the frame cuts into the image (same pixels as stamp_frame). */
static uint32_t frame_px(int x, int y) {
    if (!g_frame || !inside(&g_area, x, y)) return C_NONE;
    if (!inside(&g_box, x, y)) return C_MASK;
    int lx = g_box.w < g_area.w && (x < g_box.x + 2 || x >= g_box.x + g_box.w - 2);
    int ly = g_box.h < g_area.h && (y < g_box.y + 2 || y >= g_box.y + g_box.h - 2);
    return lx || ly ? C_LINE : C_NONE;
}

static void fill(volatile uint16_t *b, int x, int y, int w, int h, uint16_t c) {
    for (int r = y; r < y + h; r++)
        for (int k = x; k < x + w; k++) b[r * W + k] = c;
}

static void stamp_frame(volatile uint16_t *b) {
    if (!g_frame) return;
    const rect_t a = g_area, f = g_box;
    fill(b, a.x, a.y, a.w, f.y - a.y, C_MASK);                              /* above */
    fill(b, a.x, f.y + f.h, a.w, a.y + a.h - f.y - f.h, C_MASK);            /* below */
    fill(b, a.x, f.y, f.x - a.x, f.h, C_MASK);                              /* left */
    fill(b, f.x + f.w, f.y, a.x + a.w - f.x - f.w, f.h, C_MASK);            /* right */
    if (f.h < a.h) {                                                        /* lines only where the frame cuts */
        fill(b, f.x, f.y, f.w, 2, C_LINE);
        fill(b, f.x, f.y + f.h - 2, f.w, 2, C_LINE);
    }
    if (f.w < a.w) {
        fill(b, f.x, f.y, 2, f.h, C_LINE);
        fill(b, f.x + f.w - 2, f.y, 2, f.h, C_LINE);
    }
}

/* Hot cells get animated stripes (redrawn every pass); cells that went cold get
 * the stock canvas and the frame overlay back. */
static void zebra_pass(volatile uint16_t *fb) {
    uint32_t cv_addr = RGB_CANVAS;
    if ((cv_addr & 0x1FFFFFFFu) < 0x00020000u || GR2_LV_DISP_MODE != 1) return;
    const volatile uint16_t *cv = (const volatile uint16_t *)GR2_UNCACHED(cv_addr);
    const volatile uint32_t *Y = (const volatile uint32_t *)GR2_UNCACHED(GR2_LV_DISP_ADDR);
    uint32_t thr = ZEBRA_Y[g_zebra];
    int r0 = g_area.y >> 2, r1 = (g_area.y + g_area.h) >> 2, c0 = g_area.x >> 2, c1 = (g_area.x + g_area.w) >> 2;
    g_phase = (g_phase + 2) & 7;
    for (int r = r0; r < r1; r++) {
        const volatile uint32_t *row = Y + r * 4 * (W / 4);
        for (int c = c0; c < c1; c++) {
            uint32_t w = row[c];
            int hot = (w >> 24) >= thr || ((w >> 16) & 0xFF) >= thr || ((w >> 8) & 0xFF) >= thr ||
                      (w & 0xFF) >= thr;
            if (!hot && !g_zcell[r][c]) continue;
            for (int k = 0; k < 4; k++) {
                int y = 4 * r + k;
                for (int j = 0; j < 4; j++) {
                    int x = 4 * c + j;
                    uint32_t p;
                    if (hot) {
                        p = ((x + y + g_phase) & 4) ? 0xFFFFu : 0x000Fu;
                    } else {
                        p = frame_px(x, y);
                        if (p == C_NONE) p = cv[y * CV_PITCH + x];
                    }
                    fb[y * W + x] = (uint16_t)p;
                }
            }
            g_zcell[r][c] = (uint8_t)hot;
        }
        if ((r & 31) == 0) GR2_FN_DELAY(1);
    }
}

static void clear_zcells(void) {
    for (int r = 0; r < H / 4; r++)
        for (int c = 0; c < W / 4; c++) g_zcell[r][c] = 0;
}

/* Remove everything TTY drew from the shown buffer and let stock rebuild its
 * own grid/mask pixels (it flips to a fresh copy of its canvas); the next poll
 * stamps the current overlays again. */
static void wipe(void) {
    if (fullplane_live()) {
        geometry();
        fill(front(), 0, 0, W, H, C_CLEAR);
        REC_RGB_REDRAW = 1;
    }
    g_drawn = 0;
    g_last = 0xFF;
    clear_zcells();
}

void overlay_poll(void) {
    if (g_changed) {
        g_changed = 0;
        if (g_drawn) wipe();
        g_last = 0xFF;
        clear_zcells();
    }
    if (!g_frame && !g_zebra) return;
    if (!fullplane_live()) {
        g_last = 0xFF;
        return;
    }
    uint8_t g1 = GST[1];
    if (g1 != g_last) {
        GR2_FN_DELAY(FLIP_SETTLE_MS);
        if (GST[1] != g1 || !fullplane_live()) return;
        geometry();
        stamp_frame(front());
        clear_zcells();
        g_last = g1;
        g_drawn = 1;
    }
    if (g_zebra && !cfw_ui_is_open() && ++g_zpoll >= ZEBRA_POLLS) {
        g_zpoll = 0;
        zebra_pass(front());
        g_drawn = 1;
    }
}

/* ---- hardware checks (DEV tab) ------------------------------------------------- */

void overlay_log_state(void) {
    cfw_log_hex2("ovl: G[0..3], G[4..7]", *(volatile uint32_t *)0xA04070AC, *(volatile uint32_t *)0xA04070B0);
    cfw_log_hex2("ovl: plane mask, system mode", GST_MASK, GR2_FN_PROP_GET(PROPID_SYSTEM_MODE));
    cfw_log_hex2("ovl: bitmap 2, 3", BMP[2], BMP[3]);
    cfw_log_hex2("ovl: layer 1, 2 on", (uint32_t)FN_LAYER_ON(0, 1), (uint32_t)FN_LAYER_ON(0, 2));
    cfw_log_hex2("ovl: layer 3 on, RGB canvas", (uint32_t)FN_LAYER_ON(0, 3), RGB_CANVAS);
    cfw_log_hex2("ovl: screen, REC state", SCREEN_CUR, REC_STATE);
    cfw_log_hex2("ovl: LV buffer, aspect", GR2_LV_DISP_ADDR, GR2_LV_ASPECT);
}

/* Toggle: draw the pattern, or remove it (the shown buffer keeps it otherwise
 * until stock happens to flip, which can take the whole session). */
int overlay_test_pattern(void) {
    if (g_test) {
        g_test = 0;
        wipe();
        return 0;
    }
    if (!fullplane_live()) return -1;
    volatile uint16_t *b = front();
    fill(b, 40, 40, 100, 100, 0xF00Fu);          /* opaque red */
    fill(b, 160, 40, 100, 100, 0xF008u);         /* 50 % red */
    for (int i = 0; i < 200; i++) b[(160 + i) * W + 40 + i] = 0xFFFFu;   /* 1-px diagonal */
    g_test = 1;
    return 1;
}
