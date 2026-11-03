/* Film recipes (findings 18, 19, 20). CFW task only.
 *
 * A recipe is an ASCII text file in H:/cfw/recipes with `key=value` lines
 * (# comments, CRLF). The rom build (release firmware) lists the recipes in
 * A:/cfw/recipes (none shipped there; an absent folder is normal) plus the card's:
 * a card file with the same 8.3 name replaces an A: one. README "Recipes" documents
 * the keys. Applying a recipe writes it into a My Settings slot file (MY1..MY3): the
 * slot keeps its other settings, the recipe's properties and the slot name change.
 * The recipe's look (colour matrix, hue sectors, tone curves, post matrix;
 * include/look.h) is not part of a slot: the config remembers which recipe file
 * each slot holds, and src/look.c shows that look in IQ RAM while the slot is
 * selected and the recipe's base mode is active (or the previewed recipe while
 * the Recipes overlay is open).
 *
 * The list always ends with "Stock", not a file: applying it puts every property
 * a recipe can set back to its factory value in the slot (and the slot name),
 * and forgets the slot's recipe, so the slot renders with the stock IQ data.
 *
 * The three slot looks are also cached on D:/CFW (src/store.c, finding 34): the
 * loader's first hook reads them from NAND, so the look is ready before the
 * first frame without reading recipe files, and a slot keeps its look when its
 * recipe file is no longer on the card.
 *
 * recipes_format is the parser's inverse (src/export.c writes the camera's presets
 * as recipe files): every key it writes parses back to the property value it read. */
#include <stddef.h>
#include <stdint.h>
#include "look.h"
#include "recipes.h"
#include "cfw.h"
#include "cfw_log.h"
#include "config.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"
#include "halation.h"
#include "store.h"

void *memset(void *dst, int c, size_t n);              /* src/mem.c */
void *memcpy(void *dst, const void *src, size_t n);

#define DIR_PATH  "H:/cfw/recipes"
#define ROM_DIR   "A:/cfw/recipes/"      /* rom: FindFirst on A: matches a raw path prefix (finding 29 §3) */
#define PATH_LEN  32                     /* A: full paths are at most 31 characters (finding 29 §2) */
#define NAME_LEN  RECIPE_NAME_LEN
#define FILE_READ 0x00
#define UNSET     (-128)
#define N_SLOTS   3

/* Per-slot properties (all flag bit 8; finding 19 §2). Per-mode arrays: base + mode. */
#define P_EFFECT         0x02B
#define P_IMAGE_SETTINGS 0x02C
#define P_SAT            0x02E   /* 1..9, 5 = 0 */
#define P_CON            0x046
#define P_SHARP          0x05E
#define P_VIG            0x076   /* 0..3 */
#define P_HUE            0x08E
#define P_TONE           0x0A6
#define P_CLARITY        0x0BE   /* 1..9, mode 16 only */
#define P_GLOSS          0x0D6   /* 1..9, mode 17 only */
#define P_NR             0x0EF   /* 0 off, 1 weak, 2 medium, 3 strong, 4 auto, 5 custom (finding 18 §2.A) */
#define P_NR_ISO         0x0F0   /* + 0..2: custom NR, Sv where weak / medium / strong start (0x2D0 = off) */
#define P_EXPCOMP        0x0FD   /* 16 per 1/3 EV */
#define P_WB             0x0FE
#define P_WB_SHIFT       0x0FF   /* + WB mode: (GM << 16) | (AB & 0xFFFF) */
#define P_KELVIN         0x10F
#define P_SV             0x110   /* ISO: AUTO, AUTO-HI or Sv 0xF0 (100) .. 0x270 (25600), 16 per 1/3 stop */
#define P_AH_MAX         0x111   /* ISO AUTO-HI maximum, Sv 0x120 .. 0x270 (finding 18 §2.B) */
#define P_AH_MIN         0x112   /* ISO AUTO-HI minimum, Sv 0xF0 .. 0x240 */
#define P_AH_TV          0x113   /* ISO AUTO-HI slowest shutter: Auto or Tv 0x30 (1/2) .. 0x180 (1/250) */
#define P_DRC            0x130
#define P_WB_RGAIN       0x2BB   /* manual WB gains (WB 11): measured by the camera, not a recipe value */
#define P_WB_BGAIN       0x2BC

#define HICON       7            /* Hi-Contrast B&W: contrast is a list, MAX / -1 / -2 (finding 19 §2.1.1) */
#define HICON_MAX   127          /* contrast=max, stored 0x7FFFFFFF */
#define NR_CUSTOM   5
#define WB_MANUAL   11
#define WB_KELVIN   13
#define SV_AUTO     0x989811u
#define SV_AUTO_HI  0x989813u
#define SV_MAX      0x270u       /* ISO 25600 */
#define N_ISO       25           /* ISO 100 .. 25600 in 1/3 stops */
#define N_TV        22           /* 1/2 .. 1/250 in 1/3 stops */

/* Keys a base mode exposes in the stock menus (finding 19 §2). */
#define K_SAT   0x001
#define K_CON   0x002
#define K_SHARP 0x004
#define K_VIG   0x008
#define K_HUE   0x010
#define K_TONE  0x020
#define K_CLAR  0x040
#define K_GLOSS 0x080
#define K_LOOK  0x100   /* modes whose IQ stages the CFW can replace (include/look.h) */
#define K_SCHV  (K_SAT | K_CON | K_SHARP | K_VIG)

typedef struct {
    const char *key;
    int8_t mode;
    uint8_t effect;
    uint16_t keys;
} base_t;

static const base_t BASES[] = {
    { "standard",      0,  0, K_LOOK },
    { "vivid",         1,  0, K_LOOK },
    { "setting1",      3,  0, K_SCHV | K_LOOK },
    { "setting2",      4,  0, K_SCHV | K_LOOK },
    { "bright",        2,  1, K_SCHV },
    { "bw",            5,  1, K_CON | K_SHARP | K_VIG },
    { "bw_te",         6,  1, K_TONE | K_SCHV },
    { "hicontrast_bw", 7,  1, K_CON | K_SHARP | K_VIG },
    { "cross_process", 8,  1, K_HUE | K_SCHV },
    { "positive_film", 9,  1, K_SCHV },
    { "bleach_bypass", 10, 1, K_HUE | K_SCHV },
    { "retro",         11, 1, K_SCHV },
    { "miniature",     12, 1, K_TONE | K_SAT | K_CON | K_SHARP },
    { "high_key",      13, 1, K_SAT },
    { "slight",        14, 1, K_SCHV },
    { "shift_crop",    15, 1, 0 },
    { "clarity",       16, 1, K_TONE | K_CLAR | K_SAT | K_VIG },
    { "brilliance",    17, 1, K_GLOSS | K_SCHV },
    { "vibrant",       18, 1, K_SCHV },
    { "hdr_tone",      19, 1, K_TONE | K_SAT | K_VIG },
    { "portrait",      20, 1, K_SCHV },
};
#define N_BASES ((int)(sizeof(BASES) / sizeof(BASES[0])))

typedef struct { const char *key; int8_t value; } named_t;

static const named_t VIGNETTING[] = { {"off",0}, {"weak",1}, {"medium",2}, {"strong",3}, {0,0} };
static const named_t HUE_CROSS[]  = { {"basic",1}, {"magenta",2}, {"yellow",3}, {0,0} };
static const named_t HUE_BLEACH[] = { {"normal",4}, {"warm",5}, {"cool",6}, {0,0} };
static const named_t TONE_BWTE[]  = { {"sepia",1}, {"red",2}, {"green",3}, {"blue",4}, {"purple",5}, {0,0} };
static const named_t TONE_OTHER[] = { {"bw",6}, {"sepia",1}, {"color",7}, {0,0} };
static const named_t WB_MODES[]   = {
    {"auto",0}, {"multi_auto",1}, {"daylight",2}, {"shade",3}, {"cloudy",4},
    {"incandescent1",5}, {"incandescent2",6}, {"fl_daylight",7}, {"fl_neutral",8},
    {"fl_cool",9}, {"fl_warm",10}, {"cte",12}, {"kelvin",13}, {0,0} };
static const named_t DRC_MODES[]  = { {"off",0}, {"weak",3}, {"medium",4}, {"strong",5}, {"auto",6}, {0,0} };
static const named_t NR_MODES[]   = { {"off",0}, {"weak",1}, {"medium",2}, {"strong",3}, {"auto",4},
                                      {"custom",NR_CUSTOM}, {0,0} };
static const named_t CON_HICON[]  = { {"max",HICON_MAX}, {"-1",-1}, {"-2",-2}, {0,0} };
/* Nominal ISO of Sv 0xF0 + 16 i (finding 18 §2.0); shutter Tv 0x30 + 16 i (src/settings.c TV_TXT). */
static const uint16_t ISO_NOM[N_ISO] = {
    100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000,
    6400, 8000, 10000, 12800, 16000, 20000, 25600 };
static const char *const TV_NAME[N_TV] = {
    "1/2", "1/2.5", "1/3", "1/4", "1/5", "1/6", "1/8", "1/10", "1/13", "1/15", "1/20", "1/25", "1/30",
    "1/40", "1/50", "1/60", "1/80", "1/100", "1/125", "1/160", "1/200", "1/250" };

typedef struct {
    char name[NAME_LEN];
    const base_t *base;
    int8_t sat, con, sharp, vig, hue, tone, clarity, gloss;
    int8_t wb, ab, gm, ev_thirds, drc, nr;
    int16_t kelvin;
    uint32_t nr_sv[3];     /* custom NR thresholds (Sv or GR2_SV_OFF), 0 = unset */
    uint32_t sv, ah_max, ah_min, ah_tv;   /* ISO, AUTO-HI max / min / slowest shutter, 0 = unset */
    int8_t halo_key, halo_size, halo_boost;   /* `halation` / `halation_size` / `halation_boost` given */
    look_t look;
} recipe_t;

typedef struct {
    char sfn[13];          /* 8.3 name, upper case: the recipe's identity (config rcK.*, card overrides) */
    char path[PATH_LEN];   /* where it is read from: H:/cfw/recipes/<sfn> or (rom) A:/cfw/recipes/<name>; "" Stock */
    char name[NAME_LEN];
    const base_t *base;
    look_t look;
} entry_t;

/* One MY slot's look cache entry; all bytes are defined (fill_early), so entries compare bytewise. */
typedef struct {
    char sfn[13];      /* recipe file the slot holds (config rcK.*), "" none */
    int8_t ok;         /* look present */
    int8_t mode;       /* the recipe's base colour mode */
    look_t look;
} early_t;

static const char *g_names[RECIPES_MAX + 1];
static int g_n;                        /* list entries: the recipe files, then Stock */
/* Large buffers live in one GlobalHeap block (the CFW window is small). */
typedef struct {
    entry_t list[RECIPES_MAX + 1];
    char buf[4096];        /* one recipe file */
    gr2_find_t find;
    recipe_t r;
    early_t cache[N_SLOTS];   /* the look cache as it should be (cache_update) */
} work_t;
static work_t *W;
#define g_list (W->list)
#define g_buf  (W->buf)
#define g_find (W->find)
#define g_r    (W->r)

/* Stock: the last list entry, the only one without a file. */
static int is_stock(int i) { return g_list[i].path[0] == 0; }

/* ---- small string helpers ------------------------------------------------------ */

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int ends_with_txt(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n > 4 && s[n - 4] == '.' && lower(s[n - 3]) == 't' && lower(s[n - 2]) == 'x' &&
           lower(s[n - 1]) == 't';
}

static void copy_str(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int lookup(const named_t *t, const char *v, int8_t *out) {
    for (; t->key; t++) if (str_eq(t->key, v)) { *out = t->value; return 0; }
    return -1;
}

/* "[+-]digits" -> int */
static int parse_int(const char *s, int *out) {
    int neg = 0, v = 0, digits = 0;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); digits++; }
    if (!digits || *s || v > 30000) return -1;
    *out = neg ? -v : v;
    return 0;
}

/* "[+-]E[.T]" EV -> thirds of a stop, nearest (no division: x / 10 = (x * 205) >> 11 for x < 1029). */
static int parse_ev(const char *s, int8_t *out) {
    int neg = 0, whole = 0, tenth = 0;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    if (*s < '0' || *s > '9') return -1;
    while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') return -1;
        tenth = *s++ - '0';
    }
    if (*s || whole > 4) return -1;
    int tenths = whole * 10 + tenth;
    int thirds = ((tenths * 3 + 5) * 205) >> 11;
    *out = (int8_t)(neg ? -thirds : thirds);
    return 0;
}

/* "D[.F]" (no sign, at most `places` fraction digits) -> value x 10^places in 0..hi */
static int parse_fixed(const char *s, int places, int hi, int *out) {
    int v = 0, digits = 0;
    while (*s >= '0' && *s <= '9' && v <= hi) { v = v * 10 + (*s++ - '0'); digits++; }
    int f = 0;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9' && f < places) { v = v * 10 + (*s++ - '0'); f++; }
        if (!f) return -1;
    }
    if (!digits || *s) return -1;
    for (; f < places; f++) v *= 10;
    if (v > hi) return -1;
    *out = v;
    return 0;
}

static int in_range(const char *v, int lo, int hi, int8_t *out) {
    int x;
    if (parse_int(v, &x) || x < lo || x > hi) return -1;
    *out = (int8_t)x;
    return 0;
}

/* Nominal ISO -> Sv in lo..hi; "off" -> GR2_SV_OFF where off_ok. */
static int parse_iso(const char *v, uint32_t lo, uint32_t hi, int off_ok, uint32_t *out) {
    int x;
    if (off_ok && str_eq(v, "off")) { *out = GR2_SV_OFF; return 0; }
    if (parse_int(v, &x)) return -1;
    for (int i = 0; i < N_ISO; i++) {
        uint32_t sv = GR2_SV_ISO100 + 16u * (uint32_t)i;
        if (ISO_NOM[i] == x && sv >= lo && sv <= hi) { *out = sv; return 0; }
    }
    return -1;
}

/* ---- parsing ----------------------------------------------------------------------- */

/* H:/cfw/recipes/<sfn> */
static void card_path(char *path, const char *sfn) {
    const char *d = DIR_PATH "/";
    while (*d) *path++ = *d++;
    for (int i = 0; sfn[i] && i < 12; i++) *path++ = sfn[i];
    *path = 0;
}

static int read_file(const char *path) {
    int n;
#if CFW_ROM
    if (path[0] == 'A') {              /* LoadFile: two A: directory scans, File_Read takes two per read */
        n = GR2_FN_LOAD_FILE(path, g_buf, sizeof(g_buf) - 1);
    } else
#endif
    {
        int fd = GR2_FN_FILE_OPEN(path, FILE_READ);
        if (fd == 0) return -1;
        n = GR2_FN_FILE_READ(fd, g_buf, sizeof(g_buf) - 1);
        GR2_FN_FILE_CLOSE(fd);
    }
    if (n <= 0 || n >= (int)sizeof(g_buf) - 1) return -1;   /* empty, error or too large */
    g_buf[n] = 0;
    return n;
}

/* "a,b,c,..." -> n integers in lo..hi */
static int parse_list(const char *v, int n, int lo, int hi, int16_t *out) {
    for (int i = 0; i < n; i++) {
        char num[8];
        int len = 0, x;
        while (*v == ' ') v++;
        while (*v && *v != ',' && len < 7) num[len++] = *v++;
        num[len] = 0;
        while (len && num[len - 1] == ' ') num[--len] = 0;
        if (parse_int(num, &x) || x < lo || x > hi) return -1;
        out[i] = (int16_t)x;
        if (i < n - 1 && *v++ != ',') return -1;
    }
    return *v ? -1 : 0;
}

static int set_key(recipe_t *r, const char *k, const char *v) {
    const base_t *b = r->base;
    if (str_eq(k, "name")) { copy_str(r->name, v, NAME_LEN); return r->name[0] ? 0 : -1; }
    if (str_eq(k, "base")) {
        for (int i = 0; i < N_BASES; i++) if (str_eq(BASES[i].key, v)) { r->base = &BASES[i]; return 0; }
        return -1;
    }
    if (!b) return -1;                                   /* every other key needs the base first */
    if (str_eq(k, "saturation")) return (b->keys & K_SAT) ? in_range(v, -4, 4, &r->sat) : -1;
    if (str_eq(k, "contrast")) {
        if (!(b->keys & K_CON)) return -1;
        return b->mode == HICON ? lookup(CON_HICON, v, &r->con) : in_range(v, -4, 4, &r->con);
    }
    if (str_eq(k, "sharpness"))  return (b->keys & K_SHARP) ? in_range(v, -4, 4, &r->sharp) : -1;
    if (str_eq(k, "vignetting")) return (b->keys & K_VIG) ? lookup(VIGNETTING, v, &r->vig) : -1;
    if (str_eq(k, "hue"))
        return (b->keys & K_HUE) ? lookup(b->mode == 8 ? HUE_CROSS : HUE_BLEACH, v, &r->hue) : -1;
    if (str_eq(k, "toning"))
        return (b->keys & K_TONE) ? lookup(b->mode == 6 ? TONE_BWTE : TONE_OTHER, v, &r->tone) : -1;
    if (str_eq(k, "clarity"))    return (b->keys & K_CLAR) ? in_range(v, 1, 9, &r->clarity) : -1;
    if (str_eq(k, "glossiness")) return (b->keys & K_GLOSS) ? in_range(v, 1, 9, &r->gloss) : -1;
    if (str_eq(k, "wb"))         return lookup(WB_MODES, v, &r->wb);
    if (str_eq(k, "wb_kelvin")) {
        int x;
        if (parse_int(v, &x) || x < 2500 || x > 10000) return -1;
        r->kelvin = (int16_t)x;
        return 0;
    }
    if (str_eq(k, "wb_ab"))      return in_range(v, -8, 8, &r->ab);   /* stock grid ±8 [H: box exports] */
    if (str_eq(k, "wb_gm"))      return in_range(v, -8, 8, &r->gm);
    if (str_eq(k, "exposure"))   return parse_ev(v, &r->ev_thirds);
    if (str_eq(k, "dr"))         return lookup(DRC_MODES, v, &r->drc);
    if (str_eq(k, "nr"))         return lookup(NR_MODES, v, &r->nr);
    if (str_eq(k, "nr_weak"))    return parse_iso(v, GR2_SV_ISO100, SV_MAX, 0, &r->nr_sv[0]);
    if (str_eq(k, "nr_medium"))  return parse_iso(v, GR2_SV_ISO100, SV_MAX, 1, &r->nr_sv[1]);
    if (str_eq(k, "nr_strong"))  return parse_iso(v, GR2_SV_ISO100, SV_MAX, 1, &r->nr_sv[2]);
    if (str_eq(k, "iso")) {
        if (str_eq(v, "auto")) { r->sv = SV_AUTO; return 0; }
        if (str_eq(v, "auto_hi")) { r->sv = SV_AUTO_HI; return 0; }
        return parse_iso(v, GR2_SV_ISO100, SV_MAX, 0, &r->sv);
    }
    if (str_eq(k, "auto_hi_max")) return parse_iso(v, 0x120, SV_MAX, 0, &r->ah_max);
    if (str_eq(k, "auto_hi_min")) return parse_iso(v, GR2_SV_ISO100, 0x240, 0, &r->ah_min);
    if (str_eq(k, "auto_hi_shutter")) {
        if (str_eq(v, "auto")) { r->ah_tv = GR2_TV_AUTO; return 0; }
        for (int i = 0; i < N_TV; i++)
            if (str_eq(TV_NAME[i], v)) { r->ah_tv = 0x30u + 16u * (uint32_t)i; return 0; }
        return -1;
    }
    if (!(b->keys & K_LOOK)) return -1;                  /* look keys need a matrix-rendered mode */
    look_t *l = &r->look;
    int16_t t[LOOK_KNOTS];
    if (str_eq(k, "matrix")) {
        if (parse_list(v, 9, -511, 511, l->matrix)) return -1;
        l->flags |= LOOK_MATRIX;
        return 0;
    }
    if (str_eq(k, "post_matrix")) {
        if (parse_list(v, 9, -511, 511, l->post)) return -1;
        l->flags |= LOOK_POST;
        return 0;
    }
    if (k[0] == 't' && k[1] == 'o' && k[2] == 'n' && k[3] == 'e') {   /* tone, tone_r/g/b */
        int ch = !k[4] ? -1 : (k[4] == '_' && !k[6]) ? (k[5] == 'r' ? 0 : k[5] == 'g' ? 1 : k[5] == 'b' ? 2 : 3) : 3;
        if (ch == 3 || parse_list(v, LOOK_KNOTS, 0, 255, t)) return -1;
        if (!(l->flags & LOOK_TONE))                     /* unset channels: Standard (identity) */
            for (int c = 0; c < 3; c++)
                for (int i = 0; i < LOOK_KNOTS; i++) l->tone[c][i] = (uint8_t)(i == 32 ? 255 : 8 * i);
        for (int c = 0; c < 3; c++)
            if (ch < 0 || ch == c)
                for (int i = 0; i < LOOK_KNOTS; i++) l->tone[c][i] = (uint8_t)t[i];
        l->flags |= LOOK_TONE;
        return 0;
    }
    if (k[0] == 'h' && k[1] == 'u' && k[2] == 'e' && k[3] >= '1' && k[3] <= '9') {   /* hue1..hue12 */
        int s = k[3] - '0';
        if (k[4] >= '0' && k[4] <= '9' && !k[5]) s = s * 10 + (k[4] - '0');
        else if (k[4]) return -1;
        if (s > LOOK_SECTORS) return -1;
        if (!(l->flags & LOOK_HUE))                      /* unset sectors: unchanged */
            for (int q = 0; q < LOOK_SECTORS; q++)
                for (int i = 0; i < 9; i++) l->hue[q][i] = (int16_t)(i % 4 == 0 ? 128 : 0);
        if (parse_list(v, 9, -511, 511, l->hue[s - 1])) return -1;
        l->flags |= LOOK_HUE;
        return 0;
    }
    if (str_eq(k, "halation")) {                         /* off, or strength x1000 R,G,B */
        int16_t h[3] = { 0, 0, 0 };
        if (!str_eq(v, "off") && parse_list(v, 3, 0, LOOK_HALO_MAX, h)) return -1;
        for (int c = 0; c < 3; c++) l->halo[c] = (uint16_t)h[c];
        r->halo_key = 1;
        return 0;
    }
    if (str_eq(k, "halation_size")) {                    /* first-bounce sigma, um on 35 mm film */
        int x;
        if (parse_int(v, &x) || x < LOOK_HALO_UM_MIN || x > LOOK_HALO_UM_MAX) return -1;
        l->halo_um = (uint8_t)x;
        r->halo_size = 1;
        return 0;
    }
    if (str_eq(k, "halation_boost")) {                   /* boost_ev[,boost_range[,protect_ev]] (spektrafilm) */
        static const int8_t PLACES[3] = { 1, 2, 1 };
        static const int16_t HI[3] = { LOOK_BOOST_EV_MAX, 100, LOOK_BOOST_PROTECT_MAX };
        uint8_t b[3] = { 0, LOOK_BOOST_RANGE_DEF, LOOK_BOOST_PROTECT_DEF };
        if (!*v) return -1;
        for (int i = 0; i < 3 && *v; i++) {
            char num[8];
            int len = 0, x;
            while (*v == ' ') v++;
            while (*v && *v != ',' && len < 7) num[len++] = *v++;
            num[len] = 0;
            while (len && num[len - 1] == ' ') num[--len] = 0;
            if (parse_fixed(num, PLACES[i], HI[i], &x)) return -1;
            b[i] = (uint8_t)x;
            if (*v && (*v++ != ',' || i == 2 || !*v)) return -1;
        }
        if (!b[0]) b[1] = b[2] = 0;                      /* 0 EV: no boost, as without the key */
        for (int i = 0; i < 3; i++) l->boost[i] = b[i];
        r->halo_boost = 1;
        return 0;
    }
    return -1;   /* unknown key */
}

/* Parse g_buf into r. Returns 0, or the 1-based line number of the first error. */
static int parse(recipe_t *r) {
    for (int i = 0; i < (int)sizeof(*r); i++) ((uint8_t *)r)[i] = 0;
    r->sat = r->con = r->sharp = r->vig = r->hue = r->tone = r->clarity = r->gloss = UNSET;
    r->wb = r->ab = r->gm = r->ev_thirds = r->drc = r->nr = UNSET;
    char *s = g_buf;
    int line = 0;
    while (*s) {
        char *e = s;
        line++;
        while (*e && *e != '\n') e++;
        char *next = *e ? e + 1 : e;
        *e = 0;
        char *hash = s;                                  /* strip comment, CR, spaces */
        while (*hash && *hash != '#') hash++;
        *hash = 0;
        while (hash > s && (hash[-1] == '\r' || hash[-1] == ' ' || hash[-1] == '\t')) *--hash = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (*s) {
            char *eq = s;
            while (*eq && *eq != '=') eq++;
            if (!*eq) return line;
            char *ke = eq;
            *eq = 0;
            while (ke > s && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;
            char *v = eq + 1;
            while (*v == ' ' || *v == '\t') v++;
            if (set_key(r, s, v)) return line;
        }
        s = next;
    }
    if (!r->name[0] || !r->base) return line + 1;
    if ((r->ab != UNSET || r->gm != UNSET) && r->wb == UNSET) return line + 1;   /* shift needs a WB mode */
    if ((r->wb == WB_KELVIN) != (r->kelvin != 0)) return line + 1;            /* kelvin <-> wb=kelvin */
    if (r->nr == NR_CUSTOM || r->nr_sv[0] || r->nr_sv[1] || r->nr_sv[2]) {     /* custom NR: all three, ordered */
        if (r->nr != NR_CUSTOM || !r->nr_sv[0] || !r->nr_sv[1] || !r->nr_sv[2]) return line + 1;
        if (r->nr_sv[0] > r->nr_sv[1] || r->nr_sv[1] > r->nr_sv[2]) return line + 1;
    }
    if (r->ah_min && r->ah_max && r->ah_min >= r->ah_max) return line + 1;
    if ((r->halo_size || r->halo_boost) && !r->halo_key) return line + 1;    /* size, boost need `halation` */
    look_t *l = &r->look;
    if (l->halo[0] | l->halo[1] | l->halo[2]) {
        l->flags |= LOOK_HALO;
        if (!l->halo_um) l->halo_um = LOOK_HALO_UM_DEF;
    } else {
        l->halo_um = 0;                                  /* off: no halation fields, like a recipe without the key */
        l->boost[0] = l->boost[1] = l->boost[2] = 0;
    }
    return 0;
}

static void log_file_err(const char *what, const char *sfn, uint32_t value) {
    char msg[64], *p = msg;
    while (*what) *p++ = *what++;
    for (int i = 0; sfn[i] && i < 12; i++) *p++ = sfn[i];
    *p = 0;
    cfw_log_hex(msg, value);
}

static int load(const char *path, const char *sfn, recipe_t *r) {
    if (read_file(path) < 0) {
        log_file_err("ERROR recipes: cannot read (or > 4 KB) ", sfn, 0);
        return -1;
    }
    int line = parse(r);
    if (line) {
        log_file_err("ERROR recipes: invalid line in ", sfn, (uint32_t)line);
        return -1;
    }
    return 0;
}

/* ---- scan ----------------------------------------------------------------------------- */

static void sort_list(void) {
    for (int i = 1; i < g_n; i++) {
        entry_t t = g_list[i];
        int j = i;
        while (j > 0) {
            const char *a = g_list[j - 1].name, *b = t.name;
            while (*a && lower(*a) == lower(*b)) { a++; b++; }
            if (lower(*a) <= lower(*b)) break;
            g_list[j] = g_list[j - 1];
            j--;
        }
        g_list[j] = t;
    }
}

/* ---- look cache on D: --------------------------------------------------------------- */

static early_t g_early[N_SLOTS];
static int g_cache_loaded;
static int g_cache_stale;              /* g_early differs from the D: copy (or there is none) */
static store_t g_cache = { { "D:/CFW/LOOK_A.DAT", "D:/CFW/LOOK_B.DAT" }, 0x47524C4Bu /* "GRLK" */, 0, -1 };

static void cache_load(void) {
    if (g_cache_loaded) return;
    g_cache_loaded = 1;
    if (store_load(&g_cache, g_early, sizeof(g_early)) == (int)sizeof(g_early)) return;
    memset(g_early, 0, sizeof(g_early));
    g_cache_stale = 1;
}

/* e = slot entry for file sfn with recipe base mode `mode` and look l; every byte defined. */
static void fill_early(early_t *e, const char *sfn, int mode, const look_t *l) {
    memset(e, 0, sizeof(*e));
    copy_str(e->sfn, sfn, sizeof(e->sfn));
    e->ok = l->flags != 0;
    e->mode = (int8_t)mode;
    e->look.flags = l->flags;
    e->look.halo_um = l->halo_um;
    memcpy(e->look.halo, l->halo, sizeof(l->halo));
    memcpy(e->look.boost, l->boost, sizeof(l->boost));
    memcpy(e->look.matrix, l->matrix, sizeof(l->matrix));
    memcpy(e->look.post, l->post, sizeof(l->post));
    memcpy(e->look.hue, l->hue, sizeof(l->hue));
    memcpy(e->look.tone, l->tone, sizeof(l->tone));
}

/* ---- which look is shown ---------------------------------------------------------- */

#define PREVIEW 100                    /* g_shown value: 100 + recipe index */
static int8_t g_slot_recipe[N_SLOTS] = { -1, -1, -1 };   /* g_list index per MY slot, -1 none */
static int g_preview = -1;             /* recipe previewed while the Recipes overlay is open */
static int g_shown = -1;               /* -1 stock, 0..2 slot, PREVIEW + i; -2 = re-apply */
static int g_shown_mode = -1;          /* IQ mode the look is shown in */
static uint32_t g_look_gen;            /* looks applied: src/halation.c rebuilds its tables on a change */

/* Slot k's look and base mode: from its recipe file, else from the cache (file not on the card). */
static const look_t *slot_look(int k, int *mode) {
    if (g_slot_recipe[k] >= 0) {
        const entry_t *e = &g_list[g_slot_recipe[k]];
        *mode = e->base->mode;
        return &e->look;
    }
    if (g_early[k].ok) {
        *mode = g_early[k].mode;
        return &g_early[k].look;
    }
    return 0;
}

/* What to show: stock while Stock is previewed, the previewed recipe (overlay
 * open, in the current colour mode if the CFW can change it), else the recipe
 * of the active MY slot while its base mode is active, else stock. */
static int wanted(uint32_t cm, int *k5) {
    *k5 = look_mode(cm);
    if (g_preview >= 0 && is_stock(g_preview)) return -1;
    if (g_preview >= 0 && g_list[g_preview].look.flags && *k5 >= 0) return PREVIEW + g_preview;
    int k = (int)GR2_FN_PROP_GET_RAW(GR2_PROPID_MYSETTING) - 1, mode;
    if (k >= 0 && k < N_SLOTS) {
        const look_t *l = slot_look(k, &mode);
        if (l && l->flags && cm == (uint32_t)mode) return k;
    }
    return -1;
}

void recipes_poll(void) {
    uint32_t cm = GR2_FN_PROP_GET(GR2_PROPID_COLOR_MODE);
    int k5, mode, want = wanted(cm, &k5);
    const look_t *l = want < 0 ? 0 : want >= PREVIEW ? &g_list[want - PREVIEW].look : slot_look(want, &mode);
    if (want != g_shown || (want >= 0 && k5 != g_shown_mode)) {
        if (look_apply(l, k5) != 0) return;   /* IQ data not ready yet: retry at the next poll */
        g_shown = want;
        g_shown_mode = k5;
        g_look_gen++;
        DBG_HEX2("recipes: look now for (-1 stock, 0-2 slot, 100+ preview; IQ mode)", (uint32_t)want, (uint32_t)k5);
    }
    halation_sync(l, want < 0 ? -1 : (int)cm, g_look_gen);   /* also follows contrast / saturation changes */
}

void recipes_preview(int i) {
    g_preview = (i >= 0 && i < g_n) ? i : -1;
    recipes_poll();
}

/* ---- config: recipe file per slot (rcK.0..2 = 8.3 name, 4 characters per word) ----- */

static void slot_key(char *key, int k, int w) {
    key[0] = 'r'; key[1] = 'c'; key[2] = (char)('1' + k); key[3] = '.'; key[4] = (char)('0' + w); key[5] = 0;
}

static void save_slot(int k, const char *sfn) {
    char key[6];
    for (int w = 0, i = 0; w < 3; w++) {
        uint32_t v = 0;
        for (int c = 0; c < 4; c++) {
            uint8_t ch = (uint8_t)(i < 12 ? sfn[i] : 0);
            if (ch) i++;
            v = (v << 8) | ch;
        }
        slot_key(key, k, w);
        if (v || cfg_has(key)) cfg_set(key, v);   /* a missing key reads as 0 */
    }
}

/* The 8.3 name of the recipe file remembered for slot k; 0 if none. */
static int slot_sfn(int k, char sfn[13]) {
    char key[6];
    for (int w = 0; w < 3; w++) {
        slot_key(key, k, w);
        uint32_t v = cfg_get(key, 0);
        for (int c = 0; c < 4; c++) sfn[4 * w + c] = (char)(v >> (24 - 8 * c));
    }
    sfn[12] = 0;
    return sfn[0] != 0;
}

static void load_slot(int k) {
    char sfn[13];
    g_slot_recipe[k] = -1;
    if (!slot_sfn(k, sfn)) return;
    for (int i = 0; i < g_n; i++)
        if (str_eq(g_list[i].sfn, sfn)) {
            g_slot_recipe[k] = (int8_t)i;
            return;
        }
    if (g_early[k].ok && str_eq(g_early[k].sfn, sfn))
        DBG_HEX("recipes: recipe file of MY slot not on the card, cached look kept, slot", (uint32_t)(k + 1));
    else
        ERR_HEX("recipes: recipe file of MY slot no longer on the card, slot", (uint32_t)(k + 1));
}

/* Make the D: cache match the config's slot recipes: from the scanned files where present,
 * else keep the cached entry for that file; save only if it changed. */
static void cache_update(void) {
    early_t *want = W->cache;
    for (int k = 0; k < N_SLOTS; k++) {
        char sfn[13];
        if (!slot_sfn(k, sfn)) {
            memset(&want[k], 0, sizeof(want[k]));
        } else if (g_slot_recipe[k] >= 0) {
            const entry_t *e = &g_list[g_slot_recipe[k]];
            fill_early(&want[k], sfn, e->base->mode, &e->look);
        } else if (g_early[k].ok && str_eq(g_early[k].sfn, sfn)) {
            want[k] = g_early[k];
        } else {
            memset(&want[k], 0, sizeof(want[k]));
        }
    }
    const uint8_t *a = (const uint8_t *)want, *b = (const uint8_t *)g_early;
    uint32_t i = 0;
    while (i < sizeof(g_early) && a[i] == b[i]) i++;
    if (i == sizeof(g_early) && !g_cache_stale) return;
    memcpy(g_early, want, sizeof(g_early));
    if (store_save(&g_cache, g_early, sizeof(g_early)) != 0) {
        ERR("recipes: cannot write the look cache to D:/CFW");
        return;
    }
    g_cache_stale = 0;
    DBG_HEX("recipes: look cache saved to D:, generation", g_cache.gen);
}

/* ---- early look (flashed loader, boot-task hooks; M10c) ------------------------------- */

/* First loader hook: the slot looks from the D: cache; a slot the cache does not hold
 * (or holds for another file) is read from its recipe file if the card is there (not rom). */
void recipes_early_load(void) {
    if (!W) W = GR2_FN_MEMORY_ALLOC(sizeof(work_t));
    if (!W) return;                    /* recipes_init reports it */
    cache_load();
    for (int k = 0; k < N_SLOTS; k++) {
        char sfn[13];
        int has = slot_sfn(k, sfn);
        if (has && g_early[k].ok && str_eq(g_early[k].sfn, sfn)) continue;
        if (g_early[k].ok || g_early[k].sfn[0]) g_cache_stale = 1;
        memset(&g_early[k], 0, sizeof(g_early[k]));
#if !CFW_ROM   /* rom: the D: cache only (no card access in the boot hooks); recipes_init rebuilds the slot */
        /* quiet: a missing or bad file is reported by recipes_init in the task */
        char path[PATH_LEN];
        card_path(path, sfn);
        if (!has || read_file(path) < 0 || parse(&g_r) != 0) continue;
        fill_early(&g_early[k], sfn, g_r.base->mode, &g_r.look);
        g_cache_stale = 1;
#endif
    }
}

int recipes_early_look(void) {
    if (GR2_MODE_PROPS_FINAL != 1) return 0;          /* prop 6 may still be the stale value */
    int k = (int)GR2_FN_PROP_GET_RAW(GR2_PROPID_MYSETTING) - 1;
    if (k < 0 || k >= N_SLOTS || !g_early[k].ok) return 1;
    uint32_t cm = GR2_FN_PROP_GET(GR2_PROPID_COLOR_MODE);
    int k5 = look_mode(cm);
    if (k5 < 0 || cm != (uint32_t)g_early[k].mode) return 1;
    if (!look_iq_ready() || look_apply(&g_early[k].look, k5) != 0) return 0;
    DBG_HEX2("recipes: early look (slot, IQ mode)", (uint32_t)(k + 1), (uint32_t)k5);
    return 1;
}

/* ---- apply to a My Settings slot ------------------------------------------------------ */

typedef struct { uint16_t id; uint32_t value; } pv_t;
#define PV_MAX 32

/* Stored contrast: -4..+4 as 1..9; Hi-Contrast B&W's list MAX / -1 / -2 as stored (finding 19 §2.1.1). */
static uint32_t con_raw(int mode, int con) {
    if (mode != HICON) return (uint32_t)(con + 5);
    return con == HICON_MAX ? 0x7FFFFFFFu : (uint32_t)(int32_t)con;
}

static int recipe_props(const recipe_t *r, pv_t *pv) {
    int n = 0, m = r->base->mode;
    if (r->base->effect) {
        pv[n++] = (pv_t){ P_EFFECT, (uint32_t)m };
    } else {
        pv[n++] = (pv_t){ P_EFFECT, 0 };
        pv[n++] = (pv_t){ P_IMAGE_SETTINGS, (uint32_t)m };
    }
    if (r->sat != UNSET)     pv[n++] = (pv_t){ P_SAT + m, (uint32_t)(r->sat + 5) };
    if (r->con != UNSET)     pv[n++] = (pv_t){ P_CON + m, con_raw(m, r->con) };
    if (r->sharp != UNSET)   pv[n++] = (pv_t){ P_SHARP + m, (uint32_t)(r->sharp + 5) };
    if (r->vig != UNSET)     pv[n++] = (pv_t){ P_VIG + m, (uint32_t)r->vig };
    if (r->hue != UNSET)     pv[n++] = (pv_t){ P_HUE + m, (uint32_t)r->hue };
    if (r->tone != UNSET)    pv[n++] = (pv_t){ P_TONE + m, (uint32_t)r->tone };
    if (r->clarity != UNSET) pv[n++] = (pv_t){ P_CLARITY + m, (uint32_t)r->clarity };
    if (r->gloss != UNSET)   pv[n++] = (pv_t){ P_GLOSS + m, (uint32_t)r->gloss };
    if (r->wb != UNSET) {
        pv[n++] = (pv_t){ P_WB, (uint32_t)r->wb };
        if (r->wb == WB_KELVIN) pv[n++] = (pv_t){ P_KELVIN, (uint32_t)r->kelvin };
        if (r->ab != UNSET || r->gm != UNSET) {
            int ab = r->ab == UNSET ? 0 : r->ab, gm = r->gm == UNSET ? 0 : r->gm;
            pv[n++] = (pv_t){ (uint16_t)(P_WB_SHIFT + r->wb), ((uint32_t)gm << 16) | ((uint32_t)ab & 0xFFFF) };
        }
    }
    if (r->ev_thirds != UNSET) pv[n++] = (pv_t){ P_EXPCOMP, (uint32_t)(int32_t)(r->ev_thirds * 16) };
    if (r->drc != UNSET)     pv[n++] = (pv_t){ P_DRC, (uint32_t)r->drc };
    if (r->nr != UNSET) {              /* custom thresholds first, then the mode (as src/settings.c) */
        if (r->nr == NR_CUSTOM)
            for (int i = 0; i < 3; i++) pv[n++] = (pv_t){ (uint16_t)(P_NR_ISO + i), r->nr_sv[i] };
        pv[n++] = (pv_t){ P_NR, (uint32_t)r->nr };
    }
    if (r->sv)     pv[n++] = (pv_t){ P_SV, r->sv };
    if (r->ah_max) pv[n++] = (pv_t){ P_AH_MAX, r->ah_max };
    if (r->ah_min) pv[n++] = (pv_t){ P_AH_MIN, r->ah_min };
    if (r->ah_tv)  pv[n++] = (pv_t){ P_AH_TV, r->ah_tv };
    return n;
}

/* Stock: every property recipe_props can write, all per slot (finding 19 §1.4), as inclusive ID
 * ranges: EFFECT and IMAGE_SETTINGS; saturation .. glossiness of every colour mode; NR and its
 * custom thresholds; exposure compensation, WB mode, WB compensation of every WB mode and Kelvin;
 * ISO and the ISO AUTO-HI maximum / minimum / slowest shutter; DR compensation. */
static const uint16_t STOCK_IDS[][2] = {
    { P_EFFECT, P_IMAGE_SETTINGS }, { P_SAT, P_GLOSS + 23 }, { P_NR, P_NR_ISO + 2 }, { P_EXPCOMP, P_AH_TV },
    { P_DRC, P_DRC },
};

static void put_value(uint32_t *v, uint32_t id, uint32_t value) {
    if (v) v[id] = value;
    else GR2_FN_PROP_SET(id, value);
}

/* The n recipe values, or (pv == 0) the STOCK_IDS properties at their factory values, into the
 * slot DB values v, or (v == 0) live through the stock setter. */
static void put_values(uint32_t *v, const pv_t *pv, int n) {
    if (pv) {
        for (int p = 0; p < n; p++) put_value(v, pv[p].id, pv[p].value);
        return;
    }
    for (int r = 0; r < (int)(sizeof(STOCK_IDS) / sizeof(STOCK_IDS[0])); r++)
        for (uint32_t id = STOCK_IDS[r][0]; id <= STOCK_IDS[r][1]; id++) put_value(v, id, GR2_PROP_DEFAULT(id));
}

int recipes_apply(int i, int k) {
    pv_t pv[PV_MAX];
    uint16_t name16[GR2_MY_NAME_BYTES / 2];
    if (i < 0 || i >= g_n || k < 0 || k >= N_SLOTS) return RECIPE_E_SLOT;
    if (*(volatile uint32_t *)(GR2_FN_UI_STATE() + 0x274)) return RECIPE_E_BUSY;
    int stock = is_stock(i), n = 0;
    if (stock) {                       /* the factory slot name */
        for (int c = 0; c < GR2_MY_NAME_BYTES / 4; c++) {
            uint32_t w = GR2_PROP_DEFAULT(GR2_PROPID_MY_NAME + c);
            name16[2 * c] = (uint16_t)(w >> 16);
            name16[2 * c + 1] = (uint16_t)w;
        }
    } else {
        if (load(g_list[i].path, g_list[i].sfn, &g_r)) return RECIPE_E_FILE;
        n = recipe_props(&g_r, pv);
        for (int c = 0; c < GR2_MY_NAME_BYTES / 2; c++) name16[c] = 0;
        for (int c = 0; g_r.name[c] && c < 32; c++) name16[c] = (uint8_t)g_r.name[c];
    }

    /* Slot file: clone the live DB object, load the slot, change the recipe values. */
    const char *path = GR2_FN_PROP_PATH(1, k);
    void *db = GR2_FN_PDB_CLONE(GR2_LIVE_PDB);
    GR2_FN_PDB_LOAD(db, path, 1);
    uint32_t *v = GR2_PDB_VALUES(db);
    put_values(v, stock ? 0 : pv, n);
    for (int c = 0; c < GR2_MY_NAME_BYTES / 4; c++)
        v[GR2_PROPID_MY_NAME + c] = ((uint32_t)name16[2 * c] << 16) | name16[2 * c + 1];
    GR2_FN_PDB_SAVE(db, path);
    GR2_FN_PDB_FREE(db);

    /* Menu name cache, box link, and the live DB if the dial is on this slot. */
    uint16_t *cache = (uint16_t *)GR2_FN_PROP_GET(GR2_PROPID_MY_NAME_CACHE + k);
    if (cache) for (int c = 0; c < GR2_MY_NAME_BYTES / 2; c++) cache[c] = name16[c];
    GR2_FN_PROP_LOCK();
    GR2_FN_PROP_SET(GR2_PROPID_MY_LINK + k, 0);
    if (GR2_FN_PROP_GET_RAW(GR2_PROPID_MYSETTING) == (uint32_t)(k + 1)) {
        put_values(0, stock ? 0 : pv, n);
        GR2_FN_PROP_SA_WRITE(GR2_PROPID_MY_NAME, GR2_MY_NAME_BYTES, name16);
    }
    GR2_FN_PROP_UNLOCK();

    /* The slot's look comes from this recipe file from now on (re-read: it may
     * have changed on the card since the scan), Stock: none; the next poll shows it. */
    if (stock) {
        save_slot(k, "");
        g_slot_recipe[k] = -1;
    } else {
        g_list[i].look = g_r.look;
        save_slot(k, g_list[i].sfn);
        g_slot_recipe[k] = (int8_t)i;
    }
    g_shown = -2;
    cfg_save();
    cache_update();
    DBG_HEX2("recipes: applied (recipe, slot)", (uint32_t)i, (uint32_t)(k + 1));
    return 0;
}

/* ---- init ------------------------------------------------------------------------------ */

const look_t *recipes_look(int i, int *mono) {
    if (i < 0 || i >= g_n) return 0;
    *mono = g_list[i].base->mode >= 5 && g_list[i].base->mode <= 7;   /* B&W effects */
    return &g_list[i].look;
}

static int find_sfn(const char *sfn) {
    for (int i = 0; i < g_n; i++) if (str_eq(g_list[i].sfn, sfn)) return i;
    return -1;
}

/* One listing into g_list. Card: FindFirst appends the wildcard and returns 8.3 names plus "." and "..". rom, A:: dir
 * is a raw prefix, names are full paths (sfn holds garbage). A file with the 8.3 name of a listed one replaces it
 * (the card's own copy of an A: recipe), unless it cannot be read. A missing folder lists nothing, quietly. */
static int scan(const char *dir, int rom) {
    void *h = GR2_FN_FIND_FIRST(dir, &g_find);
    if (!h) return 0;
    int full = 0;
    do {
        char sfn[13], path[PATH_LEN];
        if (g_find.attr & GR2_FIND_ATTR_DIR) continue;
        if (rom) {
            const char *b = g_find.name, *d = dir;
            while (*d && *b == *d) { b++; d++; }
            int n = 0;
            while (b[n] && b[n] != '/') n++;
            if (*d || b[n] || n > 12 || (b - g_find.name) + n >= PATH_LEN || !ends_with_txt(b)) continue;
            for (int i = 0; i <= n; i++) sfn[i] = (b[i] >= 'a' && b[i] <= 'z') ? (char)(b[i] - 32) : b[i];
            copy_str(path, g_find.name, PATH_LEN);
        } else {
            if (g_find.name[0] == '.' || !ends_with_txt(g_find.name)) continue;
            copy_str(sfn, g_find.sfn, sizeof(sfn));
            card_path(path, sfn);
        }
        int i = find_sfn(sfn);
        if (i < 0 && g_n == RECIPES_MAX) {
            full = 1;
            break;
        }
        if (load(path, sfn, &g_r)) continue;
        entry_t *e = &g_list[i < 0 ? g_n++ : i];
        copy_str(e->sfn, sfn, sizeof(e->sfn));
        copy_str(e->path, path, PATH_LEN);
        copy_str(e->name, g_r.name, NAME_LEN);
        e->base = g_r.base;
        e->look = g_r.look;
    } while (GR2_FN_FIND_NEXT(h, &g_find) == 1);
    GR2_FN_FIND_CLOSE(h);
    return full;
}

void recipes_init(void) {
    g_n = 0;
    if (!W) W = GR2_FN_MEMORY_ALLOC(sizeof(work_t));
    if (!W) {
        ERR("recipes: Memory_Alloc failed");
        return;
    }
    cache_load();
#if CFW_ROM
    if (scan(ROM_DIR, 1) | scan(DIR_PATH, 0))
#else
    if (scan(DIR_PATH, 0))
#endif
        ERR("recipes: more than 48 files, ignoring the rest");
    sort_list();
    entry_t *stock = &g_list[g_n++];  /* Stock: always last, whatever the names sort to */
    memset(stock, 0, sizeof(*stock));
    copy_str(stock->name, "Stock", NAME_LEN);
    stock->base = &BASES[0];
    for (int i = 0; i < g_n; i++) g_names[i] = g_list[i].name;
    cfw_ui_set_recipes(g_names, g_n);
    for (int k = 0; k < N_SLOTS; k++) load_slot(k);
    cache_update();
    g_preview = -1;
    g_shown = -2;   /* indices may have changed: show the slot's look again */
    DBG_HEX("recipes: loaded", (uint32_t)g_n);
}

int recipes_count(void) {
    return g_n > 0 ? g_n - 1 : 0;      /* recipe files; Stock is not one */
}

/* ---- export: property values -> recipe keys (src/export.c) ------------------------------------
 * Every key is written only with a value the parser accepts and that recipe_props turns back into
 * exactly the value read; anything else becomes a `#` comment. */

static const char *named(const named_t *t, uint32_t raw) {
    for (; t->key; t++) if ((uint32_t)(int32_t)t->value == raw) return t->key;
    return 0;
}

static void put_kv(text_t *t, const char *k, const char *v) {
    t_s(t, k);
    t_c(t, '=');
    t_s(t, v);
    t_nl(t);
}

static void put_kd(text_t *t, const char *k, int32_t v) {
    t_s(t, k);
    t_c(t, '=');
    t_d(t, v);
    t_nl(t);
}

static void put_skip(text_t *t, const char *what, uint32_t raw) {
    t_s(t, "# ");
    t_s(t, what);
    t_s(t, ": stored ");
    t_hex(t, raw);
    t_s(t, ", not a recipe value (left out)");
    t_nl(t);
}

static void put_named(text_t *t, const char *k, const named_t *tb, uint32_t raw) {
    const char *n = named(tb, raw);
    if (n) put_kv(t, k, n);
    else put_skip(t, k, raw);
}

/* A 1..9 slider: written as raw - offset (recipe_props adds it back). */
static void put_slider(text_t *t, const char *k, uint32_t raw, int offset) {
    if (raw >= 1 && raw <= 9) put_kd(t, k, (int32_t)raw - offset);
    else put_skip(t, k, raw);
}

/* ISO index of Sv in lo..hi, -1 if it is no 1/3-stop list value there. */
static int iso_index(uint32_t sv, uint32_t lo, uint32_t hi) {
    if (sv < lo || sv > hi || ((sv - GR2_SV_ISO100) & 15)) return -1;
    return (int)((sv - GR2_SV_ISO100) >> 4);
}

static void put_iso(text_t *t, const char *k, uint32_t sv) {   /* sv: a list value or GR2_SV_OFF */
    if (sv == GR2_SV_OFF) put_kv(t, k, "off");
    else put_kd(t, k, ISO_NOM[iso_index(sv, GR2_SV_ISO100, SV_MAX)]);
}

static void put_list(text_t *t, const char *k, const int16_t *v, int n) {
    t_s(t, k);
    t_c(t, '=');
    for (int i = 0; i < n; i++) {
        if (i) t_c(t, ',');
        t_d(t, v[i]);
    }
    t_nl(t);
}

/* EV compensation, 16 per 1/3 EV: "+0.3", "-1.7", "0.0" (parse_ev rounds these back to the thirds). */
static void put_ev(text_t *t, uint32_t raw) {
    int32_t ev = (int32_t)raw;
    if ((ev & 15) || ev < -192 || ev > 192) { put_skip(t, "exposure", raw); return; }
    int th = ev >> 4, a = th < 0 ? -th : th, whole = 0;
    while (a >= 3) { a -= 3; whole++; }
    t_s(t, "exposure=");
    if (th) t_c(t, th < 0 ? '-' : '+');
    t_d(t, whole);
    t_c(t, '.');
    t_c(t, a == 0 ? '0' : a == 1 ? '3' : '7');
    t_nl(t);
}

static void put_wb(text_t *t, recipe_get_t get, const void *src) {
    uint32_t wb = get(src, P_WB), kel = get(src, P_KELVIN);
    const char *n = named(WB_MODES, wb);
    if (wb == WB_MANUAL) {
        t_s(t, "# wb: manual (measured gains R ");
        t_hex(t, get(src, P_WB_RGAIN));
        t_s(t, ", B ");
        t_hex(t, get(src, P_WB_BGAIN));
        t_s(t, "), not a recipe value: measure it in the slot (left out)");
        t_nl(t);
        return;
    }
    if (!n) { put_skip(t, "wb", wb); return; }
    if (wb == WB_KELVIN && (kel < 2500 || kel > 10000)) { put_skip(t, "wb_kelvin (and wb)", kel); return; }
    put_kv(t, "wb", n);
    if (wb == WB_KELVIN) put_kd(t, "wb_kelvin", (int32_t)kel);
    uint32_t c = get(src, P_WB_SHIFT + wb);
    int ab = (int16_t)(c & 0xFFFF), gm = (int16_t)(c >> 16);
    if (ab < -8 || ab > 8 || gm < -8 || gm > 8) { put_skip(t, "wb_ab / wb_gm", c); return; }
    put_kd(t, "wb_ab", ab);
    put_kd(t, "wb_gm", gm);
}

static void put_nr(text_t *t, recipe_get_t get, const void *src) {
    uint32_t nr = get(src, P_NR);
    if (nr != NR_CUSTOM) { put_named(t, "nr", NR_MODES, nr); return; }
    uint32_t s[3];
    int ok = 1;
    for (int i = 0; i < 3; i++) {
        s[i] = get(src, P_NR_ISO + i);
        if (iso_index(s[i], GR2_SV_ISO100, SV_MAX) < 0 && !(i && s[i] == GR2_SV_OFF)) ok = 0;
    }
    if (!ok || s[0] > s[1] || s[1] > s[2]) {
        t_s(t, "# nr: custom thresholds stored ");
        for (int i = 0; i < 3; i++) { t_hex(t, s[i]); t_c(t, ' '); }
        t_s(t, "are not recipe values (left out)");
        t_nl(t);
        return;
    }
    put_kv(t, "nr", "custom");
    put_iso(t, "nr_weak", s[0]);
    put_iso(t, "nr_medium", s[1]);
    put_iso(t, "nr_strong", s[2]);
}

static void put_iso_keys(text_t *t, recipe_get_t get, const void *src) {
    uint32_t sv = get(src, P_SV), mx = get(src, P_AH_MAX), mn = get(src, P_AH_MIN), tv = get(src, P_AH_TV);
    if (sv == SV_AUTO) put_kv(t, "iso", "auto");
    else if (sv == SV_AUTO_HI) put_kv(t, "iso", "auto_hi");
    else if (iso_index(sv, GR2_SV_ISO100, SV_MAX) >= 0) put_iso(t, "iso", sv);
    else put_skip(t, "iso", sv);
    if (iso_index(mx, 0x120, SV_MAX) >= 0 && iso_index(mn, GR2_SV_ISO100, 0x240) >= 0 && mn < mx) {
        put_iso(t, "auto_hi_max", mx);
        put_iso(t, "auto_hi_min", mn);
    } else {
        put_skip(t, "auto_hi_max", mx);
        put_skip(t, "auto_hi_min", mn);
    }
    if (tv == GR2_TV_AUTO) put_kv(t, "auto_hi_shutter", "auto");
    else if (tv >= 0x30 && tv <= 0x180 && !(tv & 15)) put_kv(t, "auto_hi_shutter", TV_NAME[(tv - 0x30) >> 4]);
    else put_skip(t, "auto_hi_shutter", tv);
}

static void put_look(text_t *t, int k, const base_t *b) {
    int lm;
    char sfn[13];
    const look_t *l = slot_look(k, &lm);
    if (!l || !l->flags) return;
    if (!slot_sfn(k, sfn)) sfn[0] = 0;
    t_s(t, "# MY");
    t_c(t, (char)('1' + k));
    t_s(t, " holds the look of recipe ");
    t_s(t, sfn);
    if (!(b->keys & K_LOOK) || lm != b->mode) {
        t_s(t, " for another base: look keys left out");
        t_nl(t);
        return;
    }
    t_s(t, ": written out below, so this file carries the whole look");
    t_nl(t);
    if (l->flags & LOOK_MATRIX) put_list(t, "matrix", l->matrix, 9);
    if (l->flags & LOOK_TONE)
        for (int c = 0; c < 3; c++) {
            t_s(t, c == 0 ? "tone_r=" : c == 1 ? "tone_g=" : "tone_b=");
            for (int i = 0; i < LOOK_KNOTS; i++) {
                if (i) t_c(t, ',');
                t_d(t, l->tone[c][i]);
            }
            t_nl(t);
        }
    if (l->flags & LOOK_POST) put_list(t, "post_matrix", l->post, 9);
    if (l->flags & LOOK_HUE)
        for (int q = 0; q < LOOK_SECTORS; q++) {
            char key[6] = { 'h', 'u', 'e', 0, 0, 0 };
            if (q >= 9) { key[3] = '1'; key[4] = (char)('0' + q - 9); }
            else key[3] = (char)('1' + q);
            put_list(t, key, l->hue[q], 9);
        }
    if (l->flags & LOOK_HALO) {
        t_s(t, "halation=");
        for (int c = 0; c < 3; c++) {
            if (c) t_c(t, ',');
            t_d(t, l->halo[c]);
        }
        t_nl(t);
        put_kd(t, "halation_size", l->halo_um);
        if (l->boost[0]) {                               /* boost_ev x10, boost_range x100, protect_ev x10 */
            static const int16_t DIV[3] = { 10, 100, 10 };
            t_s(t, "halation_boost=");
            for (int i = 0; i < 3; i++) {
                int v = l->boost[i], d = DIV[i], w = 0;
                while (v >= d) { v -= d; w++; }
                if (i) t_c(t, ',');
                t_d(t, w);
                t_c(t, '.');
                if (d == 100) {                          /* v < 100: two digits */
                    int tens = 0;
                    while (v >= 10) { v -= 10; tens++; }
                    t_c(t, (char)('0' + tens));
                }
                t_c(t, (char)('0' + v));
            }
            t_nl(t);
        }
    }
}

static const base_t *base_of(uint32_t mode, int effect) {
    for (int i = 0; i < N_BASES; i++)
        if ((uint32_t)BASES[i].mode == mode && (effect < 0 || BASES[i].effect == effect)) return &BASES[i];
    return 0;
}

int recipes_format(text_t *t, recipe_get_t get, const void *src, const char *name, int slot, int mode) {
    uint32_t eff = get(src, P_EFFECT), ims = get(src, P_IMAGE_SETTINGS);
    const base_t *b = mode >= 0 ? base_of((uint32_t)mode, -1) : eff ? base_of(eff, 1) : base_of(ims, 0);
    if (!b) return -1;
    int m = b->mode;
    put_kv(t, "name", name);
    put_kv(t, "base", b->key);
    if (mode < 0 && b->effect) {
        const base_t *u = base_of(ims, 0);
        t_s(t, "# Image Settings under the effect: ");
        t_s(t, u ? u->key : "?");
        t_s(t, " (not a key: the slot you apply this to keeps its own)");
        t_nl(t);
    }
    if (b->keys & K_SAT) put_slider(t, "saturation", get(src, P_SAT + m), 5);
    if (b->keys & K_CON) {
        uint32_t c = get(src, P_CON + m);
        if (m != HICON) {
            put_slider(t, "contrast", c, 5);
        } else {
            const named_t *n = CON_HICON;
            while (n->key && con_raw(HICON, n->value) != c) n++;
            if (n->key) put_kv(t, "contrast", n->key);
            else put_skip(t, "contrast", c);
        }
    }
    if (b->keys & K_SHARP) put_slider(t, "sharpness", get(src, P_SHARP + m), 5);
    if (b->keys & K_VIG) put_named(t, "vignetting", VIGNETTING, get(src, P_VIG + m));
    if (b->keys & K_HUE) put_named(t, "hue", m == 8 ? HUE_CROSS : HUE_BLEACH, get(src, P_HUE + m));
    if (b->keys & K_TONE) put_named(t, "toning", m == 6 ? TONE_BWTE : TONE_OTHER, get(src, P_TONE + m));
    if (b->keys & K_CLAR) put_slider(t, "clarity", get(src, P_CLARITY + m), 0);
    if (b->keys & K_GLOSS) put_slider(t, "glossiness", get(src, P_GLOSS + m), 0);
    if (mode >= 0) return 0;
    put_wb(t, get, src);
    put_ev(t, get(src, P_EXPCOMP));
    put_named(t, "dr", DRC_MODES, get(src, P_DRC));
    put_nr(t, get, src);
    put_iso_keys(t, get, src);
    if (slot >= 0 && slot < N_SLOTS) put_look(t, slot, b);
    return 0;
}

int recipes_preset(int i, recipe_get_t get, const void *src, const char **key, int *changed) {
    static const uint16_t SLIDER[8] = { P_SAT, P_CON, P_SHARP, P_VIG, P_HUE, P_TONE, P_CLARITY, P_GLOSS };  /* K_* bits */
    for (int j = 0; j < N_BASES; j++) {
        const base_t *b = &BASES[j];
        if (!(b->keys & 0xFF) || i--) continue;
        *changed = 0;
        for (int q = 0; q < 8; q++) {
            uint32_t id = SLIDER[q] + (uint32_t)b->mode;
            if ((b->keys & (1u << q)) && get(src, id) != GR2_PROP_DEFAULT(id)) *changed = 1;
        }
        *key = b->key;
        return b->mode;
    }
    return -1;
}
