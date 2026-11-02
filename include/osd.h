#ifndef OSD_H
#define OSD_H
#include <stdint.h>

/* Stock OSD palette indices (0x0AYYUUVV entries, hardware dump 2026-10-30;
 * RGB shown as decoded from YCbCr). */
#define OSD_TRANSPARENT 0
#define OSD_WHITE       1     /* 218,218,218 */
#define OSD_BLACK       4     /* 0,0,0 */
#define OSD_GREY_DIM    5     /* 97 */
#define OSD_BG          7     /* 14 */
#define OSD_PANEL       8     /* 29 */
#define OSD_ROW_SEL     9     /* 43 */
#define OSD_GREY_VER    14    /* 87 */
#define OSD_GREY        18    /* 131 */
#define OSD_GREY_LIGHT  21    /* 160 */
#define OSD_GOLD        56    /* 215,212,105 */
#define OSD_DARK        175   /* 8 */
#define OSD_SHADOW      190   /* black, alpha 6/15: flat shadow over live view */
/* Entries the overlay reprograms while open (exact colours, e.g. recipe
 * swatches); stock values are restored by osd_close. The stock UI is not on
 * screen while the overlay is open, so their stock use does not matter. */
#define OSD_PAL_USER    224
#define OSD_PAL_USER_N  28

/* Fonts: .fnt files in H:/cfw/fonts (rom: A:/cfw/fonts), made by scripts/make_fonts.py. Without
 * the files the ROM font (32x38) is used. Extra glyphs (string escapes): */
#define GLYPH_LSAQUO "\x80"   /* single left angle quote */
#define GLYPH_RSAQUO "\x81"   /* single right angle quote */
#define GLYPH_UP     "\x82"   /* up triangle */
#define GLYPH_DOWN   "\x83"   /* down triangle */
#define GLYPH_LEFT   "\x84"   /* left triangle */
#define GLYPH_RIGHT  "\x85"   /* right triangle */
#define GLYPH_DOT    "\x86"   /* middle dot */

enum { FONT_SMALL, FONT_TAB, FONT_ROW, FONT_ROW_BOLD, FONT_FILM, FONT_COUNT };

void osd_fonts_load(void);            /* CFW task, once */

/* Take over the OSD layer (draw lock + private buffer); 0 on success. */
int osd_open(void);
/* Give the OSD layer back to the stock UI. */
void osd_close(void);
int osd_width(void);
int osd_height(void);
void osd_fill(int x, int y, int w, int h, uint8_t color);
void osd_frame(int x, int y, int w, int h, int t, uint8_t color);   /* outline, thickness t */
int osd_line_height(int font);
int osd_text_width(int font, const char *s);
/* Draw s with its line top at y, clipped at xmax, anti-aliased against the
 * pixels underneath. Returns the x after the last glyph. */
int osd_text(int font, int x, int y, const char *s, uint8_t fg, int xmax);
/* Nearest opaque palette index to an RGB colour (valid while open). */
uint8_t osd_nearest(uint8_t r, uint8_t g, uint8_t b);
/* Program overlay-owned entry OSD_PAL_USER + k (0 <= k < OSD_PAL_USER_N) to an
 * exact RGB colour with alpha 1..15 (15 = opaque) and return its index (valid
 * while open). The change shows after osd_pal_commit (call once after drawing;
 * waits ~50 ms if anything changed). */
uint8_t osd_pal_user(int k, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
void osd_pal_commit(void);
/* The OSD layer the camera shows (640x480 palette indices, pitch 640, read
 * through the uncached alias) and its 256 palette entries (0x0AYYUUVV), e.g.
 * for a screenshot while the overlay is closed. */
const uint8_t *osd_shown_layer(uint32_t pal[256]);

#endif /* OSD_H */
