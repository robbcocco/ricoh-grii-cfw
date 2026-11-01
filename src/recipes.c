/* Film recipes (findings 19, 20). CFW task only.
 *
 * A recipe is an ASCII text file in H:/cfw/recipes with `key=value` lines
 * (# comments, CRLF). README "Recipes" documents the keys. Applying a recipe
 * writes it into a My Settings slot file (MY1..MY3): the slot keeps its other
 * settings, the recipe's properties and the slot name change. The recipe's
 * look (colour matrix, hue sectors, tone curves, post matrix; include/look.h)
 * is not part of a slot: the config remembers which recipe file each slot
 * holds, and src/look.c shows that look in IQ RAM while the slot is selected
 * and the recipe's base mode is active (or the previewed recipe while the
 * Recipes overlay is open). */
#include <stdint.h>
#include "look.h"
#include "recipes.h"
#include "cfw.h"
#include "cfw_log.h"
#include "config.h"
#include "gr2_hw.h"

#define DIR_PATH  "H:/cfw/recipes"
#define NAME_LEN  24
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
#define P_NR             0x0EF
#define P_EXPCOMP        0x0FD   /* 16 per 1/3 EV */
#define P_WB             0x0FE
#define P_WB_SHIFT       0x0FF   /* + WB mode: (GM << 16) | (AB & 0xFFFF) */
#define P_KELVIN         0x10F
#define P_DRC            0x130

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
    { "hicontrast_bw", 7,  1, K_SHARP | K_VIG },
    { "cross_process", 8,  1, K_HUE | K_SCHV },
    { "positive_film", 9,  1, K_SCHV },
    { "bleach_bypass", 10, 1, K_HUE | K_SCHV },
    { "retro",         11, 1, K_SCHV },
    { "miniature",     12, 1, K_TONE | K_SAT | K_CON | K_SHARP },
    { "high_key",      13, 1, K_SAT },
    { "slight",        14, 1, K_SCHV },
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
static const named_t NR_MODES[]   = { {"off",0}, {"weak",1}, {"medium",2}, {"strong",3}, {"auto",4}, {0,0} };

typedef struct {
    char name[NAME_LEN];
    const base_t *base;
    int8_t sat, con, sharp, vig, hue, tone, clarity, gloss;
    int8_t wb, ab, gm, ev_thirds, drc, nr;
    int16_t kelvin;
    look_t look;
} recipe_t;

typedef struct {
    char sfn[13];
    char name[NAME_LEN];
    const base_t *base;
    int8_t con;
    look_t look;
} entry_t;

static const char *g_names[RECIPES_MAX];
static int g_n;
/* Large buffers live in one GlobalHeap block (the CFW window is small). */
typedef struct {
    entry_t list[RECIPES_MAX];
    char buf[4096];        /* one recipe file */
    gr2_find_t find;
    recipe_t r;
} work_t;
static work_t *W;
#define g_list (W->list)
#define g_buf  (W->buf)
#define g_find (W->find)
#define g_r    (W->r)

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

static int in_range(const char *v, int lo, int hi, int8_t *out) {
    int x;
    if (parse_int(v, &x) || x < lo || x > hi) return -1;
    *out = (int8_t)x;
    return 0;
}

/* ---- parsing ----------------------------------------------------------------------- */

static int read_file(const char *sfn) {
    char path[48], *p = path;
    const char *d = DIR_PATH "/";
    while (*d) *p++ = *d++;
    for (int i = 0; sfn[i] && i < 12; i++) *p++ = sfn[i];
    *p = 0;
    int fd = GR2_FN_FILE_OPEN(path, FILE_READ);
    if (fd == 0) return -1;
    int n = GR2_FN_FILE_READ(fd, g_buf, sizeof(g_buf) - 1);
    GR2_FN_FILE_CLOSE(fd);
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
    if (str_eq(k, "contrast"))   return (b->keys & K_CON) ? in_range(v, -4, 4, &r->con) : -1;
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
    if (str_eq(k, "wb_ab"))      return in_range(v, -7, 7, &r->ab);
    if (str_eq(k, "wb_gm"))      return in_range(v, -7, 7, &r->gm);
    if (str_eq(k, "exposure"))   return parse_ev(v, &r->ev_thirds);
    if (str_eq(k, "dr"))         return lookup(DRC_MODES, v, &r->drc);
    if (str_eq(k, "nr"))         return lookup(NR_MODES, v, &r->nr);
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
    if ((r->wb == 13) != (r->kelvin != 0)) return line + 1;                   /* kelvin <-> wb=kelvin */
    return 0;
}

static void log_file_err(const char *what, const char *sfn, uint32_t value) {
    char msg[64], *p = msg;
    while (*what) *p++ = *what++;
    for (int i = 0; sfn[i] && i < 12; i++) *p++ = sfn[i];
    *p = 0;
    cfw_log_hex(msg, value);
}

static int load(const char *sfn, recipe_t *r) {
    if (read_file(sfn) < 0) {
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

/* ---- which look is shown ---------------------------------------------------------- */

#define PREVIEW 100                    /* g_shown value: 100 + recipe index */
static int8_t g_slot_recipe[N_SLOTS] = { -1, -1, -1 };   /* g_list index per MY slot, -1 none */
static int g_preview = -1;             /* recipe previewed while the Recipes overlay is open */
static int g_shown = -1;               /* -1 stock, 0..2 slot, PREVIEW + i; -2 = re-apply */
static int g_shown_mode = -1;          /* IQ mode the look is shown in */

/* What to show: the previewed recipe (overlay open, in the current colour
 * mode if the CFW can change it), else the recipe of the active MY slot while
 * its base mode is active, else stock. */
static int wanted(uint32_t cm, int *k5) {
    *k5 = look_mode(cm);
    if (g_preview >= 0 && g_list[g_preview].look.flags && *k5 >= 0) return PREVIEW + g_preview;
    int k = (int)GR2_FN_PROP_GET_RAW(GR2_PROPID_MYSETTING) - 1;
    if (k >= 0 && k < N_SLOTS && g_slot_recipe[k] >= 0) {
        const entry_t *e = &g_list[g_slot_recipe[k]];
        if (e->look.flags && cm == (uint32_t)e->base->mode) return k;
    }
    return -1;
}

void recipes_poll(void) {
    int k5, want = wanted(GR2_FN_PROP_GET(GR2_PROPID_COLOR_MODE), &k5);
    if (want == g_shown && (want < 0 || k5 == g_shown_mode)) return;
    const look_t *l = want < 0 ? 0 : &g_list[want >= PREVIEW ? want - PREVIEW : g_slot_recipe[want]].look;
    if (look_apply(l, k5) != 0) return;   /* IQ data not ready yet: retry at the next poll */
    g_shown = want;
    g_shown_mode = k5;
    DBG_HEX2("recipes: look now for (-1 stock, 0-2 slot, 100+ preview; IQ mode)", (uint32_t)want, (uint32_t)k5);
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
        cfg_set(key, v);
    }
}

static void load_slot(int k) {
    char key[6], sfn[13];
    for (int w = 0; w < 3; w++) {
        slot_key(key, k, w);
        uint32_t v = cfg_get(key, 0);
        for (int c = 0; c < 4; c++) sfn[4 * w + c] = (char)(v >> (24 - 8 * c));
    }
    sfn[12] = 0;
    g_slot_recipe[k] = -1;
    if (!sfn[0]) return;
    for (int i = 0; i < g_n; i++)
        if (str_eq(g_list[i].sfn, sfn)) {
            g_slot_recipe[k] = (int8_t)i;
            return;
        }
    ERR_HEX("recipes: recipe file of MY slot no longer on the card, slot", (uint32_t)(k + 1));
}

/* ---- apply to a My Settings slot ------------------------------------------------------ */

typedef struct { uint16_t id; uint32_t value; } pv_t;

static int recipe_props(const recipe_t *r, pv_t *pv) {
    int n = 0, m = r->base->mode;
    if (r->base->effect) {
        pv[n++] = (pv_t){ P_EFFECT, (uint32_t)m };
    } else {
        pv[n++] = (pv_t){ P_EFFECT, 0 };
        pv[n++] = (pv_t){ P_IMAGE_SETTINGS, (uint32_t)m };
    }
    if (r->sat != UNSET)     pv[n++] = (pv_t){ P_SAT + m, (uint32_t)(r->sat + 5) };
    if (r->con != UNSET)     pv[n++] = (pv_t){ P_CON + m, (uint32_t)(r->con + 5) };
    if (r->sharp != UNSET)   pv[n++] = (pv_t){ P_SHARP + m, (uint32_t)(r->sharp + 5) };
    if (r->vig != UNSET)     pv[n++] = (pv_t){ P_VIG + m, (uint32_t)r->vig };
    if (r->hue != UNSET)     pv[n++] = (pv_t){ P_HUE + m, (uint32_t)r->hue };
    if (r->tone != UNSET)    pv[n++] = (pv_t){ P_TONE + m, (uint32_t)r->tone };
    if (r->clarity != UNSET) pv[n++] = (pv_t){ P_CLARITY + m, (uint32_t)r->clarity };
    if (r->gloss != UNSET)   pv[n++] = (pv_t){ P_GLOSS + m, (uint32_t)r->gloss };
    if (r->wb != UNSET) {
        pv[n++] = (pv_t){ P_WB, (uint32_t)r->wb };
        if (r->wb == 13) pv[n++] = (pv_t){ P_KELVIN, (uint32_t)r->kelvin };
        if (r->ab != UNSET || r->gm != UNSET) {
            int ab = r->ab == UNSET ? 0 : r->ab, gm = r->gm == UNSET ? 0 : r->gm;
            pv[n++] = (pv_t){ (uint16_t)(P_WB_SHIFT + r->wb), ((uint32_t)gm << 16) | ((uint32_t)ab & 0xFFFF) };
        }
    }
    if (r->ev_thirds != UNSET) pv[n++] = (pv_t){ P_EXPCOMP, (uint32_t)(int32_t)(r->ev_thirds * 16) };
    if (r->drc != UNSET)     pv[n++] = (pv_t){ P_DRC, (uint32_t)r->drc };
    if (r->nr != UNSET)      pv[n++] = (pv_t){ P_NR, (uint32_t)r->nr };
    return n;
}

int recipes_apply(int i, int k) {
    pv_t pv[24];
    uint16_t name16[GR2_MY_NAME_BYTES / 2];
    if (i < 0 || i >= g_n || k < 0 || k >= N_SLOTS) return RECIPE_E_SLOT;
    if (*(volatile uint32_t *)(GR2_FN_UI_STATE() + 0x274)) return RECIPE_E_BUSY;
    if (load(g_list[i].sfn, &g_r)) return RECIPE_E_FILE;
    int n = recipe_props(&g_r, pv);
    for (int c = 0; c < GR2_MY_NAME_BYTES / 2; c++) name16[c] = 0;
    for (int c = 0; g_r.name[c] && c < 32; c++) name16[c] = (uint8_t)g_r.name[c];

    /* Slot file: clone the live DB object, load the slot, change the recipe values. */
    const char *path = GR2_FN_PROP_PATH(1, k);
    void *db = GR2_FN_PDB_CLONE(GR2_LIVE_PDB);
    GR2_FN_PDB_LOAD(db, path, 1);
    uint32_t *v = GR2_PDB_VALUES(db);
    for (int p = 0; p < n; p++) v[pv[p].id] = pv[p].value;
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
        for (int p = 0; p < n; p++) GR2_FN_PROP_SET(pv[p].id, pv[p].value);
        GR2_FN_PROP_SA_WRITE(GR2_PROPID_MY_NAME, GR2_MY_NAME_BYTES, name16);
    }
    GR2_FN_PROP_UNLOCK();

    /* The slot's look comes from this recipe file from now on (re-read: it may
     * have changed on the card since the scan); the next poll shows it. */
    g_list[i].look = g_r.look;
    save_slot(k, g_list[i].sfn);
    g_slot_recipe[k] = (int8_t)i;
    g_shown = -2;
    cfg_save();
    DBG_HEX2("recipes: applied (recipe, slot)", (uint32_t)i, (uint32_t)(k + 1));
    return 0;
}

/* ---- init ------------------------------------------------------------------------------ */

const look_t *recipes_look(int i, int *mono) {
    if (i < 0 || i >= g_n) return 0;
    *mono = g_list[i].base->mode >= 5 && g_list[i].base->mode <= 7;   /* B&W effects */
    return &g_list[i].look;
}

void recipes_init(void) {
    g_n = 0;
    if (!W) W = GR2_FN_MEMORY_ALLOC(sizeof(work_t));
    if (!W) {
        ERR("recipes: Memory_Alloc failed");
        return;
    }
    void *h = GR2_FN_FIND_FIRST(DIR_PATH, &g_find);
    if (h) {
        do {
            if (g_find.attr & GR2_FIND_ATTR_DIR) continue;
            if (g_find.name[0] == '.' || !ends_with_txt(g_find.name)) continue;
            if (g_n == RECIPES_MAX) {
                ERR("recipes: more than 24 files, ignoring the rest");
                break;
            }
            entry_t *e = &g_list[g_n];
            copy_str(e->sfn, g_find.sfn, sizeof(e->sfn));
            if (load(e->sfn, &g_r)) continue;
            copy_str(e->name, g_r.name, NAME_LEN);
            e->base = g_r.base;
            e->con = g_r.con;
            e->look = g_r.look;
            g_n++;
        } while (GR2_FN_FIND_NEXT(h, &g_find) == 1);
        GR2_FN_FIND_CLOSE(h);
    }
    sort_list();
    for (int i = 0; i < g_n; i++) g_names[i] = g_list[i].name;
    cfw_ui_set_recipes(g_names, g_n);
    for (int k = 0; k < N_SLOTS; k++) load_slot(k);
    g_preview = -1;
    g_shown = -2;   /* indices may have changed: show the slot's look again */
    DBG_HEX("recipes: loaded", (uint32_t)g_n);
}

int recipes_count(void) {
    return g_n;
}
