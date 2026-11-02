/* CFW overlay menus. Runs in the CFW task only.
 *
 * CFW Menu (fullscreen, tabbed): the tab bar is the top row. On the tab bar
 * Left/Right switch tabs and Down/OK enter the rows; on a row Up/Down move,
 * Left/Right change the value (applied at once), OK toggles two-value
 * settings, Up from the first row returns to the tab bar. The settings tabs
 * come from src/settings.c (rows grouped by function); INFO (read-only,
 * refreshed every second) and DEV (OK runs an action) from src/info.c.
 *
 * Recipes (filmstrip): live view on top previews the selected recipe's colour;
 * Left/Right browse, OK picks a My Settings slot (Left/Right MY1..MY3, OK
 * saves the recipe into it, Up/Down back).
 *
 * Both: Effect closes (and saves the config); a shutter press closes and
 * passes through to the camera. */
#include <stdint.h>
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"
#include "osd.h"
#include "info.h"
#include "recipes.h"
#include "settings.h"

#define KEY_SHUTTER 0x4201u
#define KEY_UP      0x4206u
#define KEY_DOWN    0x4207u
#define KEY_LEFT    0x4208u
#define KEY_RIGHT   0x4209u
#define KEY_OK      0x420Au
#define KEY_EFFECT  0x421Cu

/* CFW Menu layout (640x480) */
#define HEADER_H   44
#define ROWS_Y     60
#define ROW_PITCH  38
#define ROW_H      36
#define FOOTER_H   36
#define MAX_ROWS   10
#define MARGIN     20

/* Recipes layout: the filmstrip sits near the bottom of the live-view image (the
 * camera shows black below it in 3:2 and 16:9); the band above the strip only
 * appears while choosing a slot or after saving. */
#define BAND_H     36
#define STRIP_H    128
#define STRIP_GAP  24     /* between the strip and the bottom of the live-view image */
#define FRAME_W    150
#define FRAME_GAP  12
#define FILM_FRAMES 2     /* frames drawn either side of the selection (the rest are off screen) */
#define FILM_SLOTS (2 * FILM_FRAMES + 1)
/* Overlay-owned palette entries: swatches 0 .. 4*FILM_SLOTS-1, then the film base. */
#define PAL_FILM   (4 * FILM_SLOTS)
#define FILM_ALPHA 11     /* film base 11/15 opaque: live view shows through a little */

static int g_menu = -1;          /* open CFW_MENU_*, or -1 */

/* CFW Menu state (kept between openings) */
static int g_tab;
static int g_row = -1;           /* -1 = tab bar */

/* Recipes state */
static const char *const *g_recipes;
static int g_recipe_n;
static int g_recipe_sel;
static int g_apply_slot = -1;    /* slot being chosen (0..2), -1 = browsing */
static const char *g_status;     /* result of the last apply, shown in the band */

static int text_mid(int font, int top, int h) {   /* line top that centres a font in a box */
    return top + ((h - osd_line_height(font)) >> 1);
}

static int text_right(int font, int right, int y, const char *s, uint8_t fg) {
    int x = right - osd_text_width(font, s);
    osd_text(font, x, y, s, fg, right);
    return x;
}

/* ---- CFW Menu --------------------------------------------------------------- */

/* Tabs: settings tabs first, then INFO and DEV. */
static int tab_count(void) { return settings_tab_count() + INFO_PAGE_COUNT; }
static int info_page(int tab) { return tab - settings_tab_count(); }   /* < 0: settings tab */

static const char *tab_name(int tab) {
    return info_page(tab) < 0 ? settings_tab_name(tab) : info_page_name(info_page(tab));
}

static int tab_rows(int tab, int *rows) {
    int n = 0;
    if (info_page(tab) >= 0) {
        for (; n < info_rows(info_page(tab)) && n < MAX_ROWS; n++) rows[n] = n;
        return n;
    }
    for (int k = 0; k < settings_count() && n < MAX_ROWS; k++)
        if (settings_tab(settings_at(k)) == tab) rows[n++] = settings_at(k);
    return n;
}

static const char *row_label(int tab, int row) {
    return info_page(tab) < 0 ? settings_label(row) : info_label(info_page(tab), row);
}

static const char *row_value(int tab, int row) {
    return info_page(tab) < 0 ? settings_value(row) : info_value(info_page(tab), row);
}

/* 0 read-only, 1 value (Left/Right change, OK toggles two-value settings), 2 action (OK runs) */
static int row_kind(int tab, int row) {
    if (info_page(tab) < 0) return 1;
    return info_is_action(info_page(tab), row) ? 2 : 0;
}

/* Value cell of row r: what was drawn, and where the value area starts
 * (after the label), so the 1 s refresh redraws only cells that changed. */
#define VALUE_MAX 48
static char g_shown_val[MAX_ROWS][VALUE_MAX];
static int16_t g_value_x[MAX_ROWS];

static void copy_val(char *dst, const char *s) {
    int i = 0;
    for (; s[i] && i < VALUE_MAX - 1; i++) dst[i] = s[i];
    dst[i] = 0;
}

static int same_val(const char *a, const char *b) {
    int i = 0;
    for (; i < VALUE_MAX - 1 && a[i] && a[i] == b[i]; i++) {}
    return i == VALUE_MAX - 1 || a[i] == b[i];
}

static void draw_value(int r, int kind, const char *v) {
    int w = osd_width(), y = ROWS_Y + r * ROW_PITCH, sel = r == g_row;
    copy_val(g_shown_val[r], v);
    if (kind == 1 && sel) {
        int font = FONT_ROW_BOLD, ry = text_mid(font, y, ROW_H);
        int vx = text_right(font, w - MARGIN, ry, GLYPH_RSAQUO, OSD_GOLD) - 10;
        vx = text_right(font, vx, ry, v, OSD_GOLD) - 10;
        text_right(font, vx, ry, GLYPH_LSAQUO, OSD_GOLD);
        return;
    }
    /* read-only values may be long: the small font, never over the label */
    int font = kind == 0 ? FONT_SMALL : FONT_ROW;
    uint8_t fg = sel && kind == 2 ? OSD_GOLD : OSD_GREY_LIGHT;
    int vx = w - MARGIN - osd_text_width(font, v);
    if (vx < g_value_x[r]) vx = g_value_x[r];
    osd_text(font, vx, text_mid(font, y, ROW_H), v, fg, w - MARGIN);
}

static void render_settings(void) {
    int rows[MAX_ROWS];
    int n = tab_rows(g_tab, rows);
    int w = osd_width(), h = osd_height();
    osd_fill(0, 0, w, h, OSD_BLACK);
    osd_fill(0, 0, w, HEADER_H, OSD_BG);
    int x = 16, ty = text_mid(FONT_TAB, 0, HEADER_H - 4);
    for (int t = 0; t < tab_count(); t++) {
        const char *name = tab_name(t);
        int tw = osd_text_width(FONT_TAB, name);
        uint8_t fg = t != g_tab ? OSD_GREY_DIM : g_row < 0 ? OSD_GOLD : OSD_WHITE;
        osd_text(FONT_TAB, x, ty, name, fg, w);
        if (t == g_tab) osd_fill(x, HEADER_H - 4, tw, 3, OSD_GOLD);
        x += tw + 24;
    }
    text_right(FONT_SMALL, w - 16, text_mid(FONT_SMALL, 0, HEADER_H - 4), "v" CFW_VERSION, OSD_GREY_VER);

    for (int r = 0; r < n; r++) {
        int y = ROWS_Y + r * ROW_PITCH, sel = r == g_row, kind = row_kind(g_tab, rows[r]);
        if (sel) {
            osd_fill(0, y, w, ROW_H, OSD_ROW_SEL);
            osd_fill(0, y, 4, ROW_H, OSD_GOLD);
        } else {
            osd_fill(MARGIN, y + ROW_H, w - 2 * MARGIN, 1, OSD_PANEL);
        }
        const char *label = row_label(g_tab, rows[r]);
        int lx = osd_text(FONT_ROW, MARGIN, text_mid(FONT_ROW, y, ROW_H), label, OSD_WHITE, w);
        g_value_x[r] = (int16_t)(lx + 16);
        draw_value(r, kind, row_value(g_tab, rows[r]));
    }

    const char *hint = GLYPH_UP GLYPH_DOWN " select     " GLYPH_LEFT GLYPH_RIGHT " change     EFFECT close";
    if (g_row >= 0 && info_page(g_tab) < 0 && settings_is_toggle(rows[g_row]))
        hint = GLYPH_UP GLYPH_DOWN " select     " GLYPH_LEFT GLYPH_RIGHT " / OK change     EFFECT close";
    if (g_row < 0) hint = GLYPH_LEFT GLYPH_RIGHT " tab     " GLYPH_DOWN " rows     EFFECT close";
    else if (info_page(g_tab) >= 0)
        hint = row_kind(g_tab, rows[g_row]) == 2 ? GLYPH_UP GLYPH_DOWN " select     OK run     EFFECT close"
                                                 : GLYPH_UP GLYPH_DOWN " select     EFFECT close";
    osd_fill(0, h - FOOTER_H, w, FOOTER_H, OSD_BG);
    osd_text(FONT_SMALL, 16, text_mid(FONT_SMALL, h - FOOTER_H, FOOTER_H), hint, OSD_GREY, w);
}

static void close_ui(void);


static void key_settings(uint32_t id) {
    int rows[MAX_ROWS];
    int n = tab_rows(g_tab, rows);
    int tabs = tab_count();
    if (g_row < 0) {
        switch (id) {
        case KEY_LEFT:  g_tab = g_tab == 0 ? tabs - 1 : g_tab - 1; break;
        case KEY_RIGHT: g_tab = g_tab == tabs - 1 ? 0 : g_tab + 1; break;
        case KEY_DOWN:
        case KEY_OK:    if (n > 0) g_row = 0; break;
        default: return;
        }
    } else {
        switch (id) {
        case KEY_UP:    g_row--; break;                       /* row 0 -> tab bar */
        case KEY_DOWN:  g_row = g_row == n - 1 ? 0 : g_row + 1; break;
        case KEY_LEFT:
            if (row_kind(g_tab, rows[g_row]) == 1) settings_step(rows[g_row], -1);
            break;
        case KEY_RIGHT:
            if (row_kind(g_tab, rows[g_row]) == 1) settings_step(rows[g_row], +1);
            break;
        case KEY_OK:
            if (row_kind(g_tab, rows[g_row]) == 1) {
                if (!settings_is_toggle(rows[g_row])) return;     /* OK only toggles ON/OFF-like rows */
                settings_step(rows[g_row], +1);
            } else if (row_kind(g_tab, rows[g_row]) == 2 &&
                       info_action(info_page(g_tab), rows[g_row]) == INFO_CLOSE) {
                close_ui();
                return;
            }
            break;
        default: return;
        }
    }
    render_settings();
}

/* ---- Recipes ---------------------------------------------------------------- */

static void upper(char *dst, const char *src, int max) {
    int i = 0;
    for (; src[i] && i < max - 1; i++) dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 32) : src[i];
    dst[i] = 0;
}

/* Filmstrip swatch: four soft colours, one per hue quadrant of the camera's
 * hue-sector stage (magenta, red, green, cyan-blue), as Standard renders them,
 * passed through the recipe's look: colour matrix and that colour's hue sector
 * in linear light (gamma 2), then tone curves and post matrix in display
 * space. The change from Standard is exaggerated SWATCH_GAIN times so each look
 * reads at a glance. Exact colours via overlay-owned palette entries. */
#define SWATCH_GAIN 2
static const uint8_t SWATCH_BASE[4][3] = { {200, 135, 190}, {215, 125, 110}, {125, 180, 125}, {115, 165, 205} };

/* Hue sector 0..11 of an RGB colour: 30-degree steps of atan2(Cr, Cb) from +Cb
 * (sectors 0-2 magenta, 3-5 red/yellow, 6-8 green, 9-11 cyan/blue; finding 20). */
static int hue_sector(const uint8_t *c) {
    int y = (77 * c[0] + 150 * c[1] + 29 * c[2]) >> 8, cb = c[2] - y, cr = c[0] - y, q, x, h;
    if (cr >= 0 && cb >= 0) { q = 0; x = cb; h = cr; }
    else if (cr >= 0)       { q = 1; x = cr; h = -cb; }
    else if (cb < 0)        { q = 2; x = -cb; h = -cr; }
    else                    { q = 3; x = -cr; h = cb; }
    return 3 * q + (h * 256 < x * 148 ? 0 : h * 256 < x * 443 ? 1 : 2);   /* tan 30, tan 60 */
}

static int isqrt(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (int)r;
}

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void swatch(const look_t *l, int mono, int band, int dim, uint8_t out[3]) {
    const uint8_t *c = SWATCH_BASE[band];
    int v[3] = { c[0], c[1], c[2] }, o[3];
    if (mono) {
        v[0] = v[1] = v[2] = (77 * c[0] + 150 * c[1] + 29 * c[2]) >> 8;
    } else if (l) {
        int lin[3] = { v[0] * v[0], v[1] * v[1], v[2] * v[2] };
        const int16_t *ms[2] = { (l->flags & LOOK_MATRIX) ? l->matrix : 0,
                                 (l->flags & LOOK_HUE) ? l->hue[hue_sector(c)] : 0 };
        for (int s = 0; s < 2; s++) {
            if (!ms[s]) continue;
            int t[3];
            for (int r = 0; r < 3; r++) t[r] = (ms[s][3 * r] * lin[0] + ms[s][3 * r + 1] * lin[1] + ms[s][3 * r + 2] * lin[2]) >> 7;
            for (int r = 0; r < 3; r++) lin[r] = t[r] < 0 ? 0 : t[r] > 65025 ? 65025 : t[r];
        }
        for (int r = 0; r < 3; r++) v[r] = isqrt((uint32_t)lin[r]);
        if (l->flags & LOOK_TONE)
            for (int r = 0; r < 3; r++) v[r] = look_tone_at(l->tone[r], v[r]);
        if (l->flags & LOOK_POST) {
            int p[3];
            for (int r = 0; r < 3; r++)
                p[r] = clamp255((l->post[3 * r] * v[0] + l->post[3 * r + 1] * v[1] + l->post[3 * r + 2] * v[2]) >> 7);
            for (int r = 0; r < 3; r++) v[r] = p[r];
        }
    }
    for (int r = 0; r < 3; r++) o[r] = clamp255(c[r] + SWATCH_GAIN * (v[r] - c[r]));
    for (int r = 0; r < 3; r++) out[r] = (uint8_t)(dim ? (o[r] * 200) >> 8 : o[r]);
}

/* Palette slot of recipe i's swatches: by recipe, not by frame position, so a
 * scroll reprograms only the recipe entering the strip and the two whose
 * selection dimming changes. */
static int swatch_slot(int i) {
    while (i >= FILM_SLOTS) i -= FILM_SLOTS;
    return i;
}

/* Program the film base and the visible recipes' swatches, and show them,
 * before any pixel uses them: a pixel drawn first would show the entry's stock
 * colour until the commit (the green flash on opening, 2026-11-01). */
static void recipes_palette(void) {
    osd_pal_user(PAL_FILM, 29, 29, 29, FILM_ALPHA);
    for (int k = -FILM_FRAMES; k <= FILM_FRAMES; k++) {
        int i = g_recipe_sel + k, mono = 0;
        if (i < 0 || i >= g_recipe_n) continue;
        const look_t *l = recipes_look(i, &mono);
        for (int b = 0; b < 4; b++) {
            uint8_t rgb[3];
            swatch(l, mono, b, k != 0, rgb);
            osd_pal_user(swatch_slot(i) * 4 + b, rgb[0], rgb[1], rgb[2], 15);
        }
    }
    osd_pal_commit();
}

static void render_swatches(int i, int x, int y, int w, int h) {
    int bw = w >> 2;
    for (int b = 0; b < 4; b++)
        osd_fill(x + b * bw, y, b == 3 ? w - 3 * bw : bw, h, OSD_PAL_USER + swatch_slot(i) * 4 + b);
}

/* Recipe name in a frame: one line, or split at the last space that fits into
 * two tighter lines (names are upper case, so 20 px line spacing is enough). */
#define NAME_LINE2 20
static void render_film_name(const char *s, int x, int y, int h, uint8_t fg, int xmax) {
    char name[24];
    upper(name, s, sizeof(name));
    int lh = osd_line_height(FONT_FILM);
    if (osd_text_width(FONT_FILM, name) <= xmax - x) {
        osd_text(FONT_FILM, x, y + ((h - lh) >> 1), name, fg, xmax);
        return;
    }
    int cut = -1;
    for (int j = 1; name[j]; j++) {
        if (name[j] != ' ') continue;
        name[j] = 0;
        int fits = osd_text_width(FONT_FILM, name) <= xmax - x;
        name[j] = ' ';
        if (!fits) break;
        cut = j;
    }
    if (cut < 0) {                              /* one long word: clipped */
        osd_text(FONT_FILM, x, y + ((h - lh) >> 1), name, fg, xmax);
        return;
    }
    name[cut] = 0;
    int y1 = y + ((h - lh - NAME_LINE2) >> 1);
    osd_text(FONT_FILM, x, y1, name, fg, xmax);
    osd_text(FONT_FILM, x, y1 + NAME_LINE2, name + cut + 1, fg, xmax);
}

/* Top of the strip: STRIP_GAP above the bottom of the live-view image for the
 * current aspect ratio (finding 23 §4.1). */
static int strip_top(int h) {
    int bottom = GR2_LV_ASPECT == 0 ? 420 : GR2_LV_ASPECT == 2 ? 452 : h;
    return (bottom > h ? h : bottom) - STRIP_GAP - STRIP_H;
}

/* "PORTRA 400   APPLY TO [MY1] [MY2] [MY3]" while choosing a slot, or the result
 * of the last apply until the next key. */
static void render_band(int w, int y) {
    if (g_apply_slot < 0 && !g_status) return;
    osd_fill(0, y, w, BAND_H, OSD_SHADOW);
    int ty = text_mid(FONT_TAB, y, BAND_H), sy = text_mid(FONT_SMALL, y, BAND_H);
    if (g_status) {
        text_right(FONT_TAB, w - 16, ty, g_status, OSD_GOLD);
        return;
    }
    char name[24];
    upper(name, g_recipes[g_recipe_sel], sizeof(name));
    static const char *const SLOT[3] = { "MY1", "MY2", "MY3" };
    int right = w - 12;
    for (int k = 2; k >= 0; k--) {
        int cw = osd_text_width(FONT_TAB, SLOT[k]) + 16;
        int cx = right - cw;
        if (k == g_apply_slot) osd_fill(cx, y + 6, cw, BAND_H - 12, OSD_GOLD);
        osd_text(FONT_TAB, cx + 8, ty, SLOT[k], k == g_apply_slot ? OSD_BLACK : OSD_WHITE, w);
        right = cx - 6;
    }
    right = text_right(FONT_SMALL, right - 8, sy, "APPLY TO", OSD_GREY_LIGHT);
    osd_text(FONT_TAB, 16, ty, name, OSD_GOLD, right - 12);
}

/* Film base translucent; frames and sprocket holes solid. */
static void render_strip(int w, int y) {
    osd_fill(0, y, w, STRIP_H, OSD_PAL_USER + PAL_FILM);
    for (int x = 6; x < w; x += 22) {
        osd_fill(x, y + 6, 12, 10, OSD_DARK);
        osd_fill(x, y + STRIP_H - 16, 12, 10, OSD_DARK);
    }
    if (g_recipe_n == 0) {
        const char *msg = "No recipes in H:/cfw/recipes";
        osd_text(FONT_ROW, (w - osd_text_width(FONT_ROW, msg)) >> 1,
                 text_mid(FONT_ROW, y, STRIP_H), msg, OSD_GREY, w);
        return;
    }
    int cx = (w - FRAME_W) >> 1, fy = y + 22, fh = STRIP_H - 44;
    for (int k = -FILM_FRAMES; k <= FILM_FRAMES; k++) {
        int i = g_recipe_sel + k;
        if (i < 0 || i >= g_recipe_n) continue;
        int x = cx + k * (FRAME_W + FRAME_GAP), sel = k == 0;
        osd_fill(x, fy, FRAME_W, fh, OSD_DARK);
        render_swatches(i, x + 8, fy + 8, FRAME_W - 16, 32);
        render_film_name(g_recipes[i], x + 8, fy + 40, fh - 44, sel ? OSD_WHITE : OSD_GREY_DIM, x + FRAME_W - 6);
        if (sel) osd_frame(x - 3, fy - 3, FRAME_W + 6, fh + 6, 3, OSD_GOLD);
    }
}

static void render_recipes(void) {
    int w = osd_width(), h = osd_height(), y = strip_top(h);
    recipes_palette();
    osd_fill(0, 0, w, h, OSD_TRANSPARENT);       /* live view (previewing the recipe) */
    render_band(w, y - BAND_H - 4);
    render_strip(w, y);
}

static void key_recipes(uint32_t id) {
    if (g_apply_slot < 0) {
        switch (id) {
        case KEY_LEFT:  if (g_recipe_sel > 0) g_recipe_sel--; break;
        case KEY_RIGHT: if (g_recipe_sel < g_recipe_n - 1) g_recipe_sel++; break;
        case KEY_OK:    if (g_recipe_n > 0) g_apply_slot = 0; break;
        default: return;
        }
        g_status = 0;
        recipes_preview(g_recipe_sel);
    } else {
        switch (id) {
        case KEY_LEFT:  g_apply_slot = g_apply_slot == 0 ? 2 : g_apply_slot - 1; break;
        case KEY_RIGHT: g_apply_slot = g_apply_slot == 2 ? 0 : g_apply_slot + 1; break;
        case KEY_UP:
        case KEY_DOWN:  g_apply_slot = -1; break;
        case KEY_OK: {
            static const char *const DONE[3] = { "Saved to MY1", "Saved to MY2", "Saved to MY3" };
            int rc = recipes_apply(g_recipe_sel, g_apply_slot);
            g_status = rc == 0 ? DONE[g_apply_slot]
                     : rc == RECIPE_E_BUSY ? "MY edit in use" : "Failed: see log";
            g_apply_slot = -1;
            break;
        }
        default: return;
        }
    }
    render_recipes();
}

void cfw_ui_set_recipes(const char *const *names, int count) {
    g_recipes = names;
    g_recipe_n = count;
    if (g_recipe_sel >= count) g_recipe_sel = count > 0 ? count - 1 : 0;
    g_apply_slot = -1;
}

/* ---- Open / close / keys ----------------------------------------------------- */

static void render(void) {
    if (g_menu == CFW_MENU_SETTINGS) render_settings();
    else render_recipes();
}

static void open_ui(int menu) {
    if (osd_open() != 0) return;
    cfw_input_capture(1);
    g_menu = menu;
    g_apply_slot = -1;
    g_status = 0;
    if (menu == CFW_MENU_RECIPES) recipes_preview(g_recipe_sel);
    render();
    DBG_HEX("ui: open menu", (uint32_t)menu);
}

static void close_ui(void) {
    if (g_menu == CFW_MENU_RECIPES) recipes_preview(-1);
    cfw_input_capture(0);
    osd_close();
    g_menu = -1;
    DBG("ui: close");
    settings_save();
}

void cfw_ui_request(int menu) {
    if (g_menu >= 0) close_ui();
    else open_ui(menu);
}

int cfw_ui_is_open(void) {
    return g_menu >= 0;
}

/* Once a second from the CFW task: redraw the INFO/DEV values that changed,
 * cell by cell (a full redraw would flicker: the overlay draws into the shown
 * buffer). */
void cfw_ui_tick(void) {
    if (g_menu != CFW_MENU_SETTINGS || info_page(g_tab) < 0) return;
    int rows[MAX_ROWS];
    int n = tab_rows(g_tab, rows), w = osd_width();
    for (int r = 0; r < n; r++) {
        const char *v = row_value(g_tab, rows[r]);
        if (same_val(v, g_shown_val[r])) continue;
        int y = ROWS_Y + r * ROW_PITCH;
        osd_fill(g_value_x[r], y, w - g_value_x[r], ROW_H, r == g_row ? OSD_ROW_SEL : OSD_BLACK);
        draw_value(r, row_kind(g_tab, rows[r]), v);
    }
}

void cfw_ui_key(uint32_t id, uint32_t state) {
    if (g_menu < 0 || state == 0) return;         /* act on presses only */
    if (id == KEY_EFFECT || id == KEY_SHUTTER) {
        close_ui();
        return;
    }
    if (g_menu == CFW_MENU_SETTINGS) key_settings(id);
    else key_recipes(id);
}
