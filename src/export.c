/* Export the camera's saved presets as recipe files (README "Exporting your presets"; findings 18,
 * 19). CFW task only (the DEV row; the developer console asks through export_request).
 *
 * Presets (finding 19 §1.1): My Settings MY1..MY3 (B:/P_MY0k.bin) and the six My Settings Box
 * entries (B:/P_BOX0b.bin). Each file is read without touching the live settings: clone the live
 * property DB, load the file into the clone, read the values, free the clone; nothing is saved. The
 * loader falls back to the factory values without telling, so a file that does not exist (a slot or
 * box never saved) is found with File_Open first and skipped. Then the live settings (stored values;
 * where a CFW Menu setting holds a property, the camera's own value it replaced, src/settings.c),
 * and the image presets the camera keeps outside the slots: Image Settings Setting1 and Setting2
 * always, an effect's sliders when they differ from the factory values. Those come from the live
 * settings when the dial is not on MY, else from B:/P_COMMON.bin, where the camera keeps the
 * non-MY settings meanwhile (finding 19 §3).
 *
 * Output: one recipe file per preset in H:/cfw/export (not scanned by the Recipes list), rewritten
 * by every export. The keys come from recipes_format (src/recipes.c), which writes only values that
 * apply back exactly; what a recipe cannot hold (shooting mode, focus, drive, ...) is a comment. */
#include <stdint.h>
#include "export.h"
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"
#include "recipes.h"
#include "settings.h"

#define FILE_READ   0x00
#define FILE_WRITE  0x01
#define CFW_DIR     "H:/cfw"
#define OUT_DIR     "H:/cfw/export"
#define KIND_COMMON 0                    /* GR2_FN_PROP_PATH types (finding 19 §1.1) */
#define KIND_MY     1
#define KIND_BOX    2
#define N_MY        3
#define N_BOX       6
#define NAME16      (GR2_MY_NAME_BYTES / 2)
#define P_MY_SHOOTING_MODE 0x015         /* shooting mode saved in a slot / box file */

/* The file image: one GlobalHeap block, allocated by the first export (never at boot). */
typedef struct {
    char buf[RECIPE_FILE_MAX];
} xwork_t;
static xwork_t *X;
static char g_result[32];
static volatile uint32_t g_requested, g_served;

/* ---- property sources --------------------------------------------------------------------- */

static uint32_t file_get(const void *src, uint32_t id) {
    return ((const uint32_t *)src)[id];
}

static uint32_t live_get(const void *src, uint32_t id) {
    (void)src;
    uint32_t v;
    return settings_camera_value(id, &v) ? v : GR2_FN_PROP_GET_RAW(id);
}

/* A slot / box file into a clone of the live DB (finding 19 §5): its values, or 0 if the file does
 * not exist. *db must be freed with GR2_FN_PDB_FREE. */
static const uint32_t *load_file(int kind, int idx, void **db, const char **path) {
    *path = GR2_FN_PROP_PATH(kind, idx);
    int fd = GR2_FN_FILE_OPEN(*path, FILE_READ);
    if (!fd) return 0;
    GR2_FN_FILE_CLOSE(fd);
    *db = GR2_FN_PDB_CLONE(GR2_LIVE_PDB);
    GR2_FN_PDB_LOAD(*db, *path, 1);
    return GR2_PDB_VALUES(*db);
}

/* ---- text --------------------------------------------------------------------------------- */

/* Recipe name from a slot name (UTF-16, NUL-terminated within NAME16), or def if it is empty.
 * Returns 1 if the name had to change: longer than a recipe name, characters outside printable
 * ASCII or '#' (a comment in recipe files), spaces at either end (the parser trims them). */
static int make_name(char *out, const uint16_t *n16, const char *def) {
    int len = 0, changed = 0;
    for (int i = 0; i < NAME16 && n16[i]; i++) {
        uint16_t c = n16[i];
        if (len == RECIPE_NAME_LEN - 1) { changed = 1; break; }
        if (c < 0x20 || c > 0x7E || c == '#') { c = '_'; changed = 1; }
        if (c == ' ' && !len) { changed = 1; continue; }
        out[len++] = (char)c;
    }
    while (len && out[len - 1] == ' ') { len--; changed = 1; }
    out[len] = 0;
    if (len) return changed;
    while (*def) out[len++] = *def++;
    out[len] = 0;
    return n16[0] != 0;
}

static void words_name(uint16_t *n16, const uint32_t *w) {   /* props 0x1A9.. hold two halfwords each */
    for (int c = 0; c < NAME16 / 2; c++) {
        n16[2 * c] = (uint16_t)(w[c] >> 16);
        n16[2 * c + 1] = (uint16_t)w[c];
    }
}

static const char *const SHOOT[]   = { "-", "Auto", "P", "Av", "Tv", "TAv", "M", "Movie" };
static const char *const FOCUS[]   = { "-", "MF", "multi AF", "spot AF", "snap", "infinity", "multi-target AF",
                                       "face AF", "tracking AF", "pinpoint AF", "movie AF" };
static const char *const SNAP[]    = { "auto", "1m", "1.5m", "2m", "2.5m", "5m", "infinity" };
static const char *const METER[]   = { "multi", "center", "spot" };
static const char *const DRIVE[]   = { "single", "continuous", "stream", "memory", "MP low", "MP high", "HS low",
                                       "HS high", "AF continuous", "AE bracket", "AE bracket 1/3", "WB bracket",
                                       "WB preset bracket", "DR bracket", "contrast bracket", "effect bracket",
                                       "interval", "image bracket", "colour bracket", "ISO bracket",
                                       "focus bracket" };
static const char *const ASPECT[]  = { "16:9", "4:3", "3:2", "1:1" };
static const char *const CROP[]    = { "off", "35mm", "47mm", "XS" };
static const char *const SIZE[]    = { "RAW", "RAW+JPEG", "L", "M", "S", "XS" };
static const char *const QUALITY[] = { "uncompressed", "fine", "normal" };
static const char *const ND[]      = { "off", "auto", "on" };

typedef struct { uint16_t id; const char *label; const char *const *names; uint8_t n; } scope_t;
#define NAMES(a) a, (uint8_t)(sizeof(a) / sizeof(a[0]))
/* Per-slot settings a recipe does not set (finding 25 enums); the first row's ID is the source's. */
static const scope_t SCOPE[] = {
    { 0,     "shooting mode",   NAMES(SHOOT) },
    { 0x026, "focus",           NAMES(FOCUS) },
    { 0x029, "snap distance",   NAMES(SNAP) },
    { 0x02A, "metering",        NAMES(METER) },
    { 0x0F7, "drive",           NAMES(DRIVE) },
    { 0x01D, "aspect ratio",    NAMES(ASPECT) },
    { 0x021, "crop",            NAMES(CROP) },
    { 0x01E, "image size",      NAMES(SIZE) },
    { 0x01B, "JPEG quality",    NAMES(QUALITY) },
    { 0x147, "ND filter",       NAMES(ND) },
    { 0x153, "slow shutter NR (stored value)", 0, 0 },
    { 0x220, "ambient brightness (stored value)", 0, 0 },
};

static void put_scope(text_t *t, recipe_get_t get, const void *src, uint32_t mode_id) {
    t_s(t, "# Not recipe keys (the slot you apply this file to keeps its own):");
    t_nl(t);
    for (int i = 0; i < (int)(sizeof(SCOPE) / sizeof(SCOPE[0])); i++) {
        const scope_t *s = &SCOPE[i];
        uint32_t v = get(src, i ? s->id : mode_id);
        t_s(t, "#   ");
        t_s(t, s->label);
        t_s(t, ": ");
        if (s->names && v < s->n) t_s(t, s->names[v]);
        else t_d(t, (int32_t)v);
        t_nl(t);
    }
}


static void put_header(text_t *t, const char *title) {
    uint32_t min = udiv32(GR2_FN_SYSTEM_GETTIME(), 60000);
    t_s(t, "# ");
    t_s(t, title);
    t_nl(t);
    t_s(t, "# exported by gr2_cfw " CFW_VERSION);
    if (GR2_INV_LOADED == 1) {
        t_s(t, ", power-on ");
        t_d(t, (int32_t)GR2_INV_U32(GR2_INV_POWER_ONS));
    }
    t_s(t, ", ");
    t_d(t, (int32_t)min);
    t_s(t, " min after power-on");
    t_nl(t);
    t_s(t, "# To use it: move it to cfw/recipes/ and save it into a MY slot from Recipes.");
    t_nl(t);
}

/* Properties a CFW Menu setting can hold at its own value (src/settings.c): Clarity strength,
 * High-ISO NR and its thresholds, Auto-Hi slowest shutter. */
static const uint16_t OVERRIDDEN[] = { 0x0CE, 0x0EF, 0x0F0, 0x0F1, 0x0F2, 0x113 };

static int any_override(void) {
    uint32_t v;
    for (int i = 0; i < (int)(sizeof(OVERRIDDEN) / sizeof(OVERRIDDEN[0])); i++)
        if (settings_camera_value(OVERRIDDEN[i], &v)) return 1;
    return 0;
}

/* ---- one preset ----------------------------------------------------------------------------- */

static void log_file(const char *what, const char *file) {
    char msg[80], *p = put_str(msg, what);
    put_str(p, file)[0] = 0;
    cfw_log(msg);
}

typedef struct {
    const char *file;      /* 8.3 name in cfw/export */
    const char *title;     /* first comment line */
    recipe_get_t get;
    const void *src;
    const uint16_t *n16;   /* the camera's name for it (UTF-16), or empty */
    const char *def;       /* name when it has none */
    int slot;              /* MY slot whose CFW look to add, -1 none */
    int mode;              /* -1 whole preset, else the image preset of that colour mode */
    uint32_t mode_id;      /* whole preset: property holding its shooting mode */
} preset_t;

/* cfw/export/<file>: 1 written, -1 failed (logged). */
static int put_file(const preset_t *s) {
    char name[RECIPE_NAME_LEN];
    text_t t = { X->buf, X->buf + sizeof(X->buf), 0 };
    int changed = make_name(name, s->n16, s->def);
    put_header(&t, s->title);
    if (changed) {
        t_s(&t, "# The camera's name does not fit a recipe name (23 printable characters, no #): changed.");
        t_nl(&t);
    }
    if (s->get == live_get && any_override()) {
        t_s(&t, "# A CFW Menu setting is in force (Clarity strength, High-ISO NR or Auto-Hi slowest shutter):");
        t_nl(&t);
        t_s(&t, "# the camera's own values are written, not the CFW's.");
        t_nl(&t);
    }
    if (recipes_format(&t, s->get, s->src, name, s->slot, s->mode)) {
        log_file("ERROR export: base no recipe can hold, skipped ", s->file);
        return -1;
    }
    if (s->mode < 0) put_scope(&t, s->get, s->src, s->mode_id);
    if (t.cut) {
        log_file("ERROR export: more than 4 KB, skipped ", s->file);
        return -1;
    }
    char path[32], *p = put_str(path, OUT_DIR "/");
    put_str(p, s->file)[0] = 0;
    int n = (int)(t.p - X->buf), fd = GR2_FN_FILE_OPEN(path, FILE_WRITE);
    if (!fd) {
        log_file("ERROR export: cannot create ", path);
        return -1;
    }
    int ok = GR2_FN_FILE_WRITE(fd, X->buf, (uint32_t)n) == n;
    GR2_FN_FILE_CLOSE(fd);
    if (!ok) {
        log_file("ERROR export: write failed ", path);
        return -1;
    }
    return 1;
}

/* ---- export --------------------------------------------------------------------------------- */

static int fail(const char *why) {
    put_str(g_result, why)[0] = 0;
    return -1;
}

int export_run(void) {
    static const uint16_t NO_NAME[NAME16];
    uint16_t n16[NAME16];
    char file[13], title[64], def[RECIPE_NAME_LEN];
    int written = 0, failed = 0;
    if (*(volatile uint32_t *)(GR2_FN_UI_STATE() + 0x274))   /* stock My Settings edit (finding 19 §5) */
        return fail("busy: My Settings edit");
    if (GR2_FN_FILE_MKDIR(CFW_DIR) < 0) {
        ERR("export: no card");
        return fail("no card");
    }
    if (GR2_FN_FILE_MKDIR(OUT_DIR) < 0) {
        ERR("export: cannot create H:/cfw/export");
        return fail("cannot create cfw/export");
    }
    if (!X) X = GR2_FN_MEMORY_ALLOC(sizeof(xwork_t));
    if (!X) {
        ERR("export: Memory_Alloc failed");
        return fail("no memory");
    }

    /* MY1..MY3, Box 1..6: the files as saved. */
    for (int s = 0; s < N_MY + N_BOX; s++) {
        int box = s >= N_MY, k = box ? s - N_MY : s;
        void *db;
        const char *path;
        const uint32_t *v = load_file(box ? KIND_BOX : KIND_MY, k, &db, &path);
        if (!v) continue;
        char *p = put_str(file, box ? "box0" : "my");
        *p++ = (char)('1' + k);
        put_str(p, ".txt")[0] = 0;
        p = put_str(def, box ? "Box " : "MY");
        *p++ = (char)('1' + k);
        *p = 0;
        p = put_str(title, "My Settings ");
        p = put_str(p, def);
        p = put_str(p, " (");
        p = put_str(p, path);
        put_str(p, ")")[0] = 0;
        words_name(n16, &v[GR2_PROPID_MY_NAME]);
        preset_t ps = { file, title, file_get, v, n16, def, box ? -1 : k, -1, P_MY_SHOOTING_MODE };
        int rc = put_file(&ps);
        GR2_FN_PDB_FREE(db);
        if (rc > 0) written++;
        else failed++;
    }

    /* The live settings, with the look of the active MY slot's recipe. */
    int my = (int)GR2_FN_PROP_GET_RAW(GR2_PROPID_MYSETTING) - 1;
    if (my >= N_MY) my = -1;
    char *p = put_str(title, "Live settings, dial ");
    if (my >= 0) {
        p = put_str(p, "on MY");
        *p++ = (char)('1' + my);
        *p = 0;
    } else {
        put_str(p, "not on MY")[0] = 0;
    }
    preset_t live = { "live.txt", title, live_get, 0, NO_NAME, "Live", my, -1, GR2_PROPID_SHOOTING_MODE };
    if (put_file(&live) > 0) written++;
    else failed++;

    /* Image presets outside the slots: from the live DB, or B:/P_COMMON.bin while the dial is on MY. */
    void *db = 0;
    const char *from = "live settings", *key;
    int changed;
    preset_t ip = { file, title, live_get, 0, NO_NAME, def, -1, 0, 0 };
    if (my >= 0) {
        ip.get = file_get;
        ip.src = load_file(KIND_COMMON, 0, &db, &from);
    }
    for (int i = 0; (my < 0 || ip.src) && (ip.mode = recipes_preset(i, ip.get, ip.src, &key, &changed)) >= 0; i++) {
        int m = ip.mode, set = m == 3 || m == 4, tens = (m >= 10) + (m >= 20);
        if (!set && !changed) continue;               /* Setting1/2 always, an effect only when changed */
        p = put_str(file, set ? "set" : "fx");
        if (set) {
            *p++ = (char)('1' + m - 3);
        } else {
            *p++ = (char)('0' + tens);
            *p++ = (char)('0' + m - 10 * tens);
        }
        put_str(p, ".txt")[0] = 0;
        if (set) put_udec(put_str(def, "Setting"), (uint32_t)(m - 2))[0] = 0;
        else put_str(put_str(def, "Camera "), key)[0] = 0;     /* <= 7 + 13 characters */
        p = put_str(title, "Image preset ");
        p = put_str(p, key);
        p = put_str(p, " (");
        p = put_str(p, from);
        put_str(p, ")")[0] = 0;
        if (put_file(&ip) > 0) written++;
        else failed++;
    }
    if (db) GR2_FN_PDB_FREE(db);

    p = put_udec(g_result, (uint32_t)written);
    if (failed) {
        p = put_str(p, " files, ");
        p = put_udec(p, (uint32_t)failed);
        put_str(p, " failed (log)")[0] = 0;
    } else {
        put_str(p, " files in cfw/export")[0] = 0;
    }
    DBG_HEX2("export: files written, failed", (uint32_t)written, (uint32_t)failed);
    return failed ? -1 : written;
}

const char *export_result(void) {
    return g_result;
}

uint32_t export_request(void) {
    return ++g_requested;
}

int export_served(uint32_t ticket) {
    return (int32_t)(g_served - ticket) >= 0;
}

void export_poll(void) {
    uint32_t want = g_requested;
    if (want == g_served) return;
    export_run();
    g_served = want;
}
