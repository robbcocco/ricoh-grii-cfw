/* INFO and DEV tabs of the TTY Menu (finding 21). TTY task only.
 *
 * INFO shows read-only camera state from stock getters and RAM copies:
 * inventory counters, battery level and raw ADC, thermistors, card space,
 * firmware version, serial, uptime. DEV runs developer actions: the stock
 * telnet shell and the TTY developer console (no password, started on request,
 * never persisted; Wi-Fi itself is turned on in the camera menu), a screenshot of
 * live view + OSD to the card, a recipe rescan, the preset export (src/export.c)
 * and an IQ RAM dump. */
#include <stdint.h>
#include "info.h"
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"
#include "osd.h"
#include "console.h"
#include "export.h"
#include "overlay.h"
#include "recipes.h"
#include "safeboot.h"

#define FILE_READ   0x00
#define FILE_WRITE  0x01
#define SHOT_W      640
#define SHOT_H      480
#define PENDING_DELAY 1500              /* ms after the menu closed: the shooting screen is back */
#define IQ_BYTES    0x2FAA0u            /* IQParam block (IQ.bin + 0x180) */

static char g_text[96];
static uint32_t g_main_base;
static int g_pending;                   /* DEV row whose action runs after the menu closed */
static uint32_t g_pending_at;           /* System_GetTime when to run it, 0 = none */
static char g_dev_msg[8][24];           /* last result per action row (D_TELNET..D_LAST_ACTION) */

/* ---- formatting --------------------------------------------------------------- */

static char *put_temp(char *p, const char *name, uint32_t which) {
    int v = GR2_FN_POWER_TEMP(which);
    p = put_str(p, name);
    if (v <= 0) return put_str(p, " n/a");
    *p++ = ' ';
    p = put_sdec(p, (v >> 3) - 273);
    return put_str(p, " C");
}

/* bytes (hi, lo) -> "12.3" GB */
static char *put_gb(char *p, uint32_t hi, uint32_t lo) {
    uint32_t mb = (hi << 12) | (lo >> 20);
    uint32_t tenths = (mb * 10 + 512) >> 10;
    p = put_udec(p, udiv32(tenths, 10));
    *p++ = '.';
    return put_udec(p, tenths - 10 * udiv32(tenths, 10));
}

static void copy_msg(int row, const char *s) {
    char *p = g_dev_msg[row];
    int i = 0;
    while (s[i] && i < (int)sizeof(g_dev_msg[row]) - 1) { p[i] = s[i]; i++; }
    p[i] = 0;
}

/* ---- INFO --------------------------------------------------------------------- */

enum { I_SHOTS, I_EXPOS, I_BATTERY, I_TEMP, I_CARD, I_FIRMWARE, I_UPTIME, I_POWERONS, I_COUNT };
static const char *const INFO_LABEL[I_COUNT] = {
    "Images stored", "Shutter exposures", "Battery", "Temperature", "Card", "Firmware",
    "Uptime", "Power-ons",
};

static const char *info_text(int row) {
    char *p = g_text;
    switch (row) {
    case I_SHOTS:
    case I_EXPOS:
    case I_POWERONS: {
        int id = row == I_SHOTS ? GR2_INV_SHOTS : row == I_EXPOS ? GR2_INV_EXPOSURES : GR2_INV_POWER_ONS;
        if (GR2_INV_LOADED != 1) return "n/a";
        p = put_udec(p, GR2_INV_U32(id));
        break;
    }
    case I_BATTERY: {
        /* The raw reading follows the load (live view, lens, Wi-Fi) and jumps by
         * tens of counts: show a running average (~8 s time constant at 1 Hz). */
        static const char *const LEVEL[4] = { "EMPTY", "LOW", "HALF", "FULL" };
        static uint32_t avg16;            /* average x16, 0 = no sample yet */
        uint32_t lvl = GR2_FN_PROP_GET(GR2_PROPID_BATTERY_LEVEL), ad = 0;
        GR2_FN_ADC_READ(GR2_ADC_BATTERY, &ad);
        avg16 = avg16 ? avg16 - (avg16 >> 3) + (ad << 1) : ad << 4;
        p = put_str(p, lvl < 4 ? LEVEL[lvl] : "?");
        p = put_str(p, "  " GLYPH_DOT "  AD ");
        p = put_udec(p, (avg16 + 8) >> 4);
        break;
    }
    case I_TEMP:
        p = put_temp(p, "sensor", 1);
        p = put_str(p, "  " GLYPH_DOT "  ");
        p = put_temp(p, "lens", 0);
        break;
    case I_CARD:
        p = put_gb(p, GR2_FN_PROP_GET(GR2_PROPID_CARD_TOTAL_HI + 2), GR2_FN_PROP_GET(GR2_PROPID_CARD_TOTAL_HI + 3));
        p = put_str(p, " of ");
        p = put_gb(p, GR2_FN_PROP_GET(GR2_PROPID_CARD_TOTAL_HI), GR2_FN_PROP_GET(GR2_PROPID_CARD_TOTAL_HI + 1));
        p = put_str(p, " GB free  " GLYPH_DOT "  ");
        p = put_udec(p, GR2_FN_PROP_GET(GR2_PROPID_SHOTS_LEFT));
        p = put_str(p, " shots");
        break;
    case I_FIRMWARE: {
        static const char HEX[] = "0123456789ABCDEF";
        uint32_t v = GR2_FN_PROP_GET(GR2_PROPID_APP_VERSION);
        if (v >> 28) *p++ = HEX[v >> 28];
        *p++ = HEX[(v >> 24) & 0xF];
        *p++ = '.';
        *p++ = HEX[(v >> 20) & 0xF];
        *p++ = HEX[(v >> 16) & 0xF];
        p = put_str(p, "  " GLYPH_DOT "  serial ");
        p = put_hex32(p, GR2_FN_PROP_GET(GR2_PROPID_SERIAL));
        break;
    }
    case I_UPTIME: {
        uint32_t s = udiv32(GR2_FN_SYSTEM_GETTIME(), 1000), m = udiv32(s, 60);
        if (m >= 60) {
            p = put_udec(p, udiv32(m, 60));
            p = put_str(p, " h ");
            m -= 60 * udiv32(m, 60);
        }
        p = put_udec(p, m);
        p = put_str(p, " min ");
        p = put_udec(p, s - 60 * udiv32(s, 60));
        p = put_str(p, " s");
        break;
    }
    default:
        return "";
    }
    *p = 0;
    return g_text;
}

/* ---- DEV ---------------------------------------------------------------------- */

/* The page shows 10 rows at a time (src/ui.c MAX_ROWS); longer pages scroll with the cursor. */
enum { D_TELNET, D_SHOT, D_RECIPES, D_EXPORT, D_IQDUMP, D_OVLSTATE, D_OVLTEST, D_STOCK, D_WIFI, D_MEMORY, D_COUNT };
#define D_LAST_ACTION D_STOCK
_Static_assert(sizeof(g_dev_msg) / sizeof(g_dev_msg[0]) == D_LAST_ACTION + 1, "one message per action row");
static const char *const DEV_LABEL[D_COUNT] = {
    "Telnet console", "Screenshot to card", "Reload recipes", "Export settings to card", "Save IQ RAM",
    "Overlay state to log", "Overlay test pattern", "Boot stock once", "Wi-Fi network / password",
    "Free memory pool",
};

/* Wi-Fi state as the system controller sees it (finding 21 §3.1): SYSTEM_MODE,
 * UI_MODE, CONNECT_MODE, PROPID_WIFI request, WIFI_STATUS, Ctrl+0x214 (JOBID_WIFI
 * state), +0x1B8 (controller's last posted request), +0x1BC (force flag). */
void info_wifi_state(uint32_t v[INFO_WIFI_N]) {
    const volatile uint8_t *ctrl = GR2_FN_UI_STATE();
    v[0] = GR2_FN_PROP_GET(0);
    v[1] = GR2_FN_PROP_GET(1);
    v[2] = GR2_FN_PROP_GET(2);
    v[3] = GR2_FN_PROP_GET(GR2_PROPID_WIFI);
    v[4] = GR2_FN_PROP_GET(GR2_PROPID_WIFI_STATUS);
    v[5] = *(const volatile uint32_t *)(ctrl + 0x214);
    v[6] = *(const volatile uint32_t *)(ctrl + 0x1B8);
    v[7] = *(const volatile uint32_t *)(ctrl + 0x1BC);
}

/* DEV rows that switch something:
 *  - console: OFF, or ON with where to reach it (or that Wi-Fi is still off).
 *    Wi-Fi itself is left to the camera menu: TTY's own Prop_Set(0x1E8) +
 *    Msg_Post(0x116) start reached JOBID_WIFI (state 1, status 1) but the access
 *    point never appeared on hardware (2026-11-01, finding 21).
 *  - test pattern: RUNNING between OK and the drawing after the menu closed. */
static uint8_t g_pattern_on;

static const char *dev_text(int row) {
    char *p = g_text;
    uint32_t st = GR2_FN_PROP_GET(GR2_PROPID_WIFI_STATUS);
    switch (row) {
    case D_TELNET: {
        int cs = console_state();
        if (cs == CONSOLE_OFF) return "OFF";
        return cs == CONSOLE_LISTENING && st != 0 ? "ON  " GLYPH_DOT "  telnet 192.168.0.1"
                                                  : "ON  " GLYPH_DOT "  Wi-Fi off (camera menu)";
    }
    case D_OVLTEST:
        if (g_pending_at && g_pending == D_OVLTEST) return "RUNNING";
        return g_dev_msg[row][0] ? g_dev_msg[row] : g_pattern_on ? "ON" : "OFF";
    case D_STOCK:                         /* safe boot (include/safeboot.h) */
        return sb_get() == SB_STOCK_ONCE ? "next power-on: stock" : "OK: next power-on stock";
    case D_EXPORT:
        return export_result()[0] ? export_result() : "OK to run";
    case D_WIFI: {                        /* "network  .  password" while Wi-Fi is on */
        if (st == 0) return "-";
        char buf[0x44];
        for (int k = 0; k < 2; k++) {
            uint32_t n = k ? 0x41 : 0x21;
            GR2_FN_PROP_SA_READ(k ? GR2_PROPID_WIFI_PASS : GR2_PROPID_WIFI_SSID, n, buf);
            buf[n] = 0;
            if (k) p = put_str(p, "  " GLYPH_DOT "  ");
            for (uint32_t i = 0; buf[i] && i < 32; i++) *p++ = (buf[i] >= 0x20 && buf[i] < 0x7F) ? buf[i] : '?';
        }
        break;
    }
    case D_MEMORY: {
        uint32_t used = GR2_POOL_BUMP;
        if (used < 0xA0900000u || used > GR2_POOL_LIMIT) return "?";
        p = put_udec(p, (GR2_POOL_LIMIT - used) >> 20);
        p = put_str(p, " MB  " GLYPH_DOT "  stage 2 at 0x");
        p = put_hex32(p, g_main_base);
        break;
    }
    default:
        return g_dev_msg[row][0] ? g_dev_msg[row] : "OK to run";
    }
    *p = 0;
    return g_text;
}

/* Screenshot: live view (planar YUV 4:2:2) blended with the shown OSD layer, as a
 * 24-bit BMP. The boot pool is used up on hardware and 900 KB from the shared
 * heap could assert, so the frame is copied in bands of SHOT_BAND rows (one
 * 80 KB heap block): each band comes from the frame shown at that moment,
 * copied in a few ms before the camera reuses that buffer. With the camera
 * still the bands are seamless; with movement they come from frames tens of
 * ms apart. The OSD is read in place (it does not change while idle). */
#define SHOT_BAND   48                  /* divides SHOT_H */
#define BAND_BYTES  (SHOT_BAND * (SHOT_W + SHOT_W / 2 + SHOT_W / 2))
static uint8_t *g_band;
static uint32_t g_pal[256];

static int shot_name(char *path) {
    static const char base[] = "H:/tty/shot0000.bmp";
    for (uint32_t n = 1; n < 10000; n++) {
        char *p = path;
        for (int i = 0; base[i]; i++) *p++ = base[i];
        *p = 0;
        uint32_t v = n;
        for (int d = 14; d >= 11; d--) { uint32_t q = udiv32(v, 10); path[d] = (char)('0' + v - 10 * q); v = q; }
        int fd = GR2_FN_FILE_OPEN(path, FILE_READ);
        if (!fd) return 0;
        GR2_FN_FILE_CLOSE(fd);
    }
    return -1;
}

static void put_le(uint8_t *p, uint32_t v, int n) {
    for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void yuv_rgb(int y, int u, int v, int *r, int *g, int *b) {
    u -= 128;
    v -= 128;
    *r = y + ((359 * v) >> 8);
    *g = y - ((88 * u + 183 * v) >> 8);
    *b = y + ((454 * u) >> 8);
}

static void shot_failed(const char *why) {
    copy_msg(D_SHOT, "failed: see log");
    cfw_log(why);
}

static void screenshot(void) {
    char path[24];
    if (GR2_LV_DISP_MODE != 1) { shot_failed("ERROR dev: screenshot needs the LCD"); return; }
    if (!g_band) g_band = GR2_FN_MEMORY_ALLOC(BAND_BYTES);
    if (!g_band) { shot_failed("ERROR dev: screenshot Memory_Alloc failed"); return; }
    if (shot_name(path)) { shot_failed("ERROR dev: no free screenshot name"); return; }
    int fd = GR2_FN_FILE_OPEN(path, FILE_WRITE);
    if (!fd) { shot_failed("ERROR dev: cannot create the screenshot file"); return; }
    const uint8_t *osd = osd_shown_layer(g_pal);
    uint8_t h[54] = { 'B', 'M' };
    put_le(h + 2, 54 + SHOT_W * SHOT_H * 3, 4);
    put_le(h + 10, 54, 4);
    put_le(h + 14, 40, 4);
    put_le(h + 18, SHOT_W, 4);
    put_le(h + 22, SHOT_H, 4);
    put_le(h + 26, 1, 2);
    put_le(h + 28, 24, 2);
    put_le(h + 34, SHOT_W * SHOT_H * 3, 4);
    GR2_FN_FILE_WRITE(fd, h, sizeof(h));
    static uint8_t row[SHOT_W * 3];
    uint8_t *bY = g_band, *bU = g_band + SHOT_BAND * SHOT_W, *bV = bU + SHOT_BAND * (SHOT_W / 2);
    for (int top = SHOT_H - SHOT_BAND; top >= 0; top -= SHOT_BAND) {   /* BMP rows run bottom-up */
        uint32_t a = GR2_UNCACHED(GR2_LV_DISP_ADDR);
        const volatile uint32_t *sY = (const volatile uint32_t *)(a + top * SHOT_W);
        const volatile uint32_t *sU = (const volatile uint32_t *)(a + 0x4B000 + top * (SHOT_W / 2));
        const volatile uint32_t *sV = (const volatile uint32_t *)(a + 0x70800 + top * (SHOT_W / 2));
        for (int i = 0; i < SHOT_BAND * SHOT_W / 4; i++) ((uint32_t *)bY)[i] = sY[i];
        for (int i = 0; i < SHOT_BAND * SHOT_W / 8; i++) ((uint32_t *)bU)[i] = sU[i];
        for (int i = 0; i < SHOT_BAND * SHOT_W / 8; i++) ((uint32_t *)bV)[i] = sV[i];
        for (int y = SHOT_BAND - 1; y >= 0; y--) {
            const uint8_t *Y = bY + y * SHOT_W, *U = bU + y * (SHOT_W / 2), *V = bV + y * (SHOT_W / 2);
            const uint8_t *o = osd + (top + y) * SHOT_W;
            for (int x = 0; x < SHOT_W; x++) {
                int r, g, b;
                yuv_rgb(Y[x], U[x >> 1], V[x >> 1], &r, &g, &b);
                uint32_t e = g_pal[o[x]];
                int al = (int)((e >> 24) & 0xF);
                if (al) {
                    int orr, og, ob;
                    yuv_rgb((int)((e >> 16) & 0xFF), (int)((e >> 8) & 0xFF), (int)(e & 0xFF), &orr, &og, &ob);
                    r = ((r * (15 - al) + orr * al) * 273) >> 12;   /* / 15 */
                    g = ((g * (15 - al) + og * al) * 273) >> 12;
                    b = ((b * (15 - al) + ob * al) * 273) >> 12;
                }
                row[3 * x] = (uint8_t)clamp8(b);
                row[3 * x + 1] = (uint8_t)clamp8(g);
                row[3 * x + 2] = (uint8_t)clamp8(r);
            }
            GR2_FN_FILE_WRITE(fd, row, sizeof(row));
        }
    }
    GR2_FN_FILE_CLOSE(fd);
    copy_msg(D_SHOT, path + 7);
    DBG("dev: screenshot saved");
}

static void iq_dump(void) {
    uint32_t ctx = GR2_IQ_CTX;
    int bank = ctx >= 0xA0000000u && ctx < 0xC0000000u ? *(volatile int8_t *)(ctx + 0x2C) : -1;
    if (bank < 0 || bank > 1) { copy_msg(D_IQDUMP, "IQ not loaded"); return; }
    uint32_t iq = *(volatile uint32_t *)(ctx + 0x158 + 4 * bank);
    int fd = GR2_FN_FILE_OPEN("H:/tty/iq_ram.bin", FILE_WRITE);
    if (!fd) { copy_msg(D_IQDUMP, "cannot write"); return; }
    int n = GR2_FN_FILE_WRITE(fd, (const void *)GR2_UNCACHED(iq), IQ_BYTES);
    GR2_FN_FILE_CLOSE(fd);
    copy_msg(D_IQDUMP, n == (int)IQ_BYTES ? "iq_ram.bin saved" : "write failed");
}

/* ---- tab interface ------------------------------------------------------------ */

const char *info_page_name(int page) {
    return page == INFO_PAGE_INFO ? "INFO" : "DEV";
}

int info_rows(int page) {
    return page == INFO_PAGE_INFO ? I_COUNT : D_COUNT;
}

const char *info_label(int page, int row) {
    return page == INFO_PAGE_INFO ? INFO_LABEL[row] : DEV_LABEL[row];
}

const char *info_value(int page, int row) {
    return page == INFO_PAGE_INFO ? info_text(row) : dev_text(row);
}

int info_is_action(int page, int row) {
    return page == INFO_PAGE_DEV && row <= D_LAST_ACTION;
}

int info_action(int page, int row) {
    if (!info_is_action(page, row)) return 0;
    switch (row) {
    case D_TELNET:
        console_set(console_state() == CONSOLE_OFF);
        return 0;
    case D_SHOT:
    case D_OVLSTATE:
    case D_OVLTEST:                       /* need the shooting screen: close the menu first */
        g_pending = row;
        g_pending_at = GR2_FN_SYSTEM_GETTIME() + PENDING_DELAY;
        if (!g_pending_at) g_pending_at = 1;
        if (row != D_OVLTEST) copy_msg(row, "running...");
        return INFO_CLOSE;
    case D_RECIPES: {
        recipes_init();
        char *p = g_dev_msg[D_RECIPES];
        p = put_udec(p, (uint32_t)recipes_count());
        put_str(p, " recipes")[0] = 0;
        return 0;
    }
    case D_IQDUMP:
        iq_dump();
        return 0;
    case D_EXPORT:
        export_run();
        return 0;
    case D_STOCK:
        sb_set(SB_STOCK_ONCE);
        DBG("safeboot: next power-on stock");
        return 0;
    default:
        return 0;
    }
}

void info_set_main_base(uint32_t base) {
    g_main_base = base;
}

void info_poll(void) {
    uint32_t now = GR2_FN_SYSTEM_GETTIME();
    if (!g_pending_at || cfw_ui_is_open() || (int32_t)(now - g_pending_at) < 0) return;
    int row = g_pending;
    g_pending_at = 0;
    if (row == D_SHOT) {
        screenshot();
    } else if (row == D_OVLSTATE) {
        overlay_log_state();
        copy_msg(row, "written to tty_log.txt");
    } else {
        int rc = overlay_test_pattern();
        g_pattern_on = rc > 0;
        copy_msg(row, rc < 0 ? "OFF: no shooting screen" : "");
    }
}
