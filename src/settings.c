/* M5 settings engine. Boot task (settings_early, flashed loader: the IQ-ready call before live view) or the
 * CFW task (everything else), never both at once: the task calls in only after the loader's hooks are over.
 *
 * Property settings (Clarity, Auto-ISO slowest shutter, High-ISO NR) write
 * stock-legal values through the stock setter, exactly like the camera menu.
 * Before the first write in a shooting context we record the camera's value
 * ("stock", also kept in the config file) and write it back when the setting
 * is returned to STOCK. Our value is re-applied ~1 s after the shooting mode
 * or My Settings slot changes, because MY slots reload these properties.
 * Like a stock menu change, the values are saved at power-off outside MY modes.
 *
 * RAM-patch settings (TAv ISO cap, Auto ISO slowest shutter / max ISO, Snap
 * distances) verify every original value first, write single aligned stores,
 * and are lost at power-off.
 * Evidence: findings 18 and 21. */
#include <stdint.h>
#include "settings.h"
#include "config.h"
#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"
#include "overlay.h"

enum { S_TAV, S_AISOMIN, S_AISOMAX, S_AISOHOLD, S_SNAP, S_FRAMES, S_ZEBRA, S_CLARITY,
       S_AISOTV, S_NR, S_NRW, S_NRM, S_NRS, S_TAVMIN, S_MAUTO, S_COUNT };
enum { TAB_EXPOSURE, TAB_FOCUS, TAB_VIEW, TAB_IMAGE, TAB_COUNT };

static const char *const TAB_NAME[TAB_COUNT] = { "EXPOSURE", "FOCUS", "VIEW", "IMAGE" };

typedef struct {
    const char *label;
    const char *key;
    uint8_t count;
    uint8_t def;
    uint8_t tab;
} desc_t;

/* Settings and their config keys; the enum order is stable, rows are listed in ORDER
 * (labels/values sized for the 18-px font). */
static const desc_t DESC[S_COUNT] = {
    { "TAv: cap ISO at Auto-Hi max", "tav",     2,  1,  TAB_EXPOSURE },   /* OFF, ON */
    { "Auto ISO slowest shutter",    "aisomin", 15, 0,  TAB_EXPOSURE },   /* STOCK, 1/60+crop, 1/15..1/250 */
    { "Auto ISO max",                "aisomax", 8,  0,  TAB_EXPOSURE },   /* STOCK, ISO 400..25600 */
    { "Auto ISO at max ISO (P, Av)", "aisohold", 2, 0,  TAB_EXPOSURE },   /* SLOWER SHUTTER, KEEP SHUTTER */
    { "Snap distances",              "snap",    2,  0,  TAB_FOCUS },      /* STOCK (1m, 1.5m), 0.7m + 1.2m */
    { "Frame lines",                 "frames",  FRAME_COUNT, 0, TAB_VIEW },  /* OFF, XPan, 6x7, 4:5, 1:1, 16:9 */
    { "Zebras",                      "zebra",   4,  0,  TAB_VIEW },       /* OFF, Y >= 235 / 245 / 252 */
    { "Clarity strength",            "clarity", 10, 0,  TAB_IMAGE },      /* STOCK, 1..9 */
    { "Auto-Hi slowest shutter",     "aisotv",  24, 0,  TAB_EXPOSURE },   /* STOCK, Auto, 1/2..1/250 */
    { "High-ISO NR",                 "nr",      2,  0,  TAB_IMAGE },      /* STOCK, CUSTOM */
    { "NR weak from",                "nrw",     25, 0,  TAB_IMAGE },      /* ISO 100..25600 (stock default 100) */
    { "NR medium from",              "nrm",     26, 11, TAB_IMAGE },      /* ISO 100..25600, OFF (default 1250) */
    { "NR strong from",              "nrs",     26, 17, TAB_IMAGE },      /* ISO 100..25600, OFF (default 5000) */
    { "TAv: Auto-Hi minimum too",    "tavmin",  2,  0,  TAB_EXPOSURE },   /* OFF, ON (finding 21 R3) */
    { "Auto ISO in M",               "mauto",   2,  0,  TAB_EXPOSURE },   /* OFF, ON (R5) */
};

/* Rows in display order, grouped by what they do within each tab: Auto ISO limits, then Auto-Hi, then TAv. */
static const uint8_t ORDER[S_COUNT] = {
    S_AISOMAX, S_AISOMIN, S_AISOHOLD, S_MAUTO, S_AISOTV, S_TAV, S_TAVMIN,
    S_SNAP,
    S_FRAMES, S_ZEBRA,
    S_CLARITY, S_NR, S_NRW, S_NRM, S_NRS,
};

static uint8_t g_val[S_COUNT];

#define ISO_OFF_IDX 25
static const char *const ISO_TXT[26] = {
    "ISO 100", "ISO 125", "ISO 160", "ISO 200", "ISO 250", "ISO 320", "ISO 400", "ISO 500",
    "ISO 640", "ISO 800", "ISO 1000", "ISO 1250", "ISO 1600", "ISO 2000", "ISO 2500",
    "ISO 3200", "ISO 4000", "ISO 5000", "ISO 6400", "ISO 8000", "ISO 10000", "ISO 12800",
    "ISO 16000", "ISO 20000", "ISO 25600", "OFF",
};
static const char *const TV_TXT[24] = {
    "STOCK", "Auto", "1/2", "1/2.5", "1/3", "1/4", "1/5", "1/6", "1/8", "1/10",
    "1/13", "1/15", "1/20", "1/25", "1/30", "1/40", "1/50", "1/60", "1/80",
    "1/100", "1/125", "1/160", "1/200", "1/250",
};

static uint32_t iso_sv(int idx) {
    return idx == ISO_OFF_IDX ? GR2_SV_OFF : GR2_SV_ISO100 + 16u * (uint32_t)idx;
}

static uint32_t tv_value(int idx) {           /* idx 1 = Auto, 2.. = 1/2 (0x30) .. 1/250 (0x180) */
    return idx == 1 ? GR2_TV_AUTO : 0x30u + 16u * (uint32_t)(idx - 2);
}

/* ---- Property overrides ---------------------------------------------------- */

typedef struct {
    uint16_t prop;
    uint8_t active;      /* our value is in force */
    uint8_t has_stock;
    uint32_t written;    /* value we last wrote */
    uint32_t stock;      /* camera value before our write */
    const char *stock_key;
} ovr_t;

static ovr_t o_clarity = { GR2_PROPID_CLARITY_16,         0, 0, 0, 0, "clarity.s" };
static ovr_t o_aisotv  = { GR2_PROPID_ISO_AUTO_CHANGE_TV, 0, 0, 0, 0, "aisotv.s" };
static ovr_t o_nr      = { GR2_PROPID_NOISE_REDUCTION,    0, 0, 0, 0, "nr.s" };
static ovr_t o_nrw     = { GR2_PROPID_NR_ISO + 0,         0, 0, 0, 0, "nrw.s" };
static ovr_t o_nrm     = { GR2_PROPID_NR_ISO + 1,         0, 0, 0, 0, "nrm.s" };
static ovr_t o_nrs     = { GR2_PROPID_NR_ISO + 2,         0, 0, 0, 0, "nrs.s" };
static ovr_t *const OVRS[] = { &o_clarity, &o_aisotv, &o_nr, &o_nrw, &o_nrm, &o_nrs };
#define N_OVRS ((int)(sizeof(OVRS) / sizeof(OVRS[0])))

static void prop_write(uint32_t prop, uint32_t v) {
    GR2_FN_PROP_LOCK();
    GR2_FN_PROP_SET(prop, v);
    GR2_FN_PROP_UNLOCK();
}

static void ovr_apply(ovr_t *o, uint32_t want) {
    uint32_t cur = GR2_FN_PROP_GET_RAW(o->prop);
    /* cur is a camera value (not ours) unless we wrote it and nobody changed it.
     * Equal to want with no stock yet: we have never written, so it is stock. */
    if (!(o->active && cur == o->written) && (cur != want || !o->has_stock)) {
        o->stock = cur;
        o->has_stock = 1;
        cfg_set(o->stock_key, cur);
    }
    if (cur != want) {
        prop_write(o->prop, want);
        DBG_HEX2("set: prop/value", o->prop, want);
    }
    o->written = want;
    o->active = 1;
}

static void ovr_release(ovr_t *o) {
    if (!o->active) return;
    o->active = 0;
    if (GR2_FN_PROP_GET_RAW(o->prop) != o->written) return;   /* changed in the camera menu: keep it */
    if (!o->has_stock) {
        ERR_HEX("set: cannot restore, no camera value recorded for prop", o->prop);
        return;
    }
    prop_write(o->prop, o->stock);
    DBG_HEX2("set: restored prop/value", o->prop, o->stock);
}

static void clarity_update(void) {
    if (g_val[S_CLARITY]) ovr_apply(&o_clarity, g_val[S_CLARITY]);
    else ovr_release(&o_clarity);
}

static void aisotv_update(void) {
    if (g_val[S_AISOTV]) ovr_apply(&o_aisotv, tv_value(g_val[S_AISOTV]));
    else ovr_release(&o_aisotv);
}

static void nr_update(void) {
    if (g_val[S_NR]) {               /* thresholds first, then Manual mode */
        ovr_apply(&o_nrw, iso_sv(g_val[S_NRW]));
        ovr_apply(&o_nrm, iso_sv(g_val[S_NRM]));
        ovr_apply(&o_nrs, iso_sv(g_val[S_NRS]));
        ovr_apply(&o_nr, 5);
    } else {
        ovr_release(&o_nr);
        ovr_release(&o_nrw);
        ovr_release(&o_nrm);
        ovr_release(&o_nrs);
    }
}

/* Keep weak <= medium <= strong (OFF sorts last; weak is never OFF). */
static void nr_order(int changed) {
    uint8_t *w = &g_val[S_NRW], *m = &g_val[S_NRM], *s = &g_val[S_NRS];
    if (changed == S_NRS) {
        if (*m > *s) *m = *s;
        if (*w > *m) *w = *m;
    } else {
        if (changed == S_NRM && *w > *m) *w = *m;
        if (*m < *w) *m = *w;
        if (*s < *m) *s = *m;
    }
}

/* ---- TAv ISO cap (finding 18 §3) ---------------------------------------------
 * In TAv, unless ISO is AUTO-HI with DR compensation off, the AE ceiling is
 * the tuning field tavmode_fix_high_agc_limit (AE_F table +0x132, read only at
 * 0xA01F81BC) instead of the ISO Auto-High max. We keep that field equal to
 * the Auto-High max: agc = Sv / 3 - 0x50 (16 per stop, 0 = ISO 100). */
#define AEF_TABLE_LIT  ((volatile uint32_t *)0xA01E7A2C)   /* literal: AE_F table base */
#define AEF_TABLE      0xA070FC48u
#define TAV_LIMIT      ((volatile uint16_t *)(AEF_TABLE + 0x132))
#define TAV_READER     ((volatile uint16_t *)0xA01F81B8)   /* adds r0,#0xff; adds r0,#0x21; ldrh r0,[r0,#0x12] */

static int g_tav_on, g_tav_err, g_tav_have;
static uint16_t g_tav_stock;

static void tav_update(void) {
    if (!g_val[S_TAV]) {
        if (g_tav_on) {
            *TAV_LIMIT = g_tav_stock;
            g_tav_on = 0;
            DBG_HEX("tav: restored limit", g_tav_stock);
        }
        return;
    }
    if (g_tav_err) return;
    if (!g_tav_have) {
        if (*AEF_TABLE_LIT != AEF_TABLE || TAV_READER[0] != 0x30FF || TAV_READER[1] != 0x3021 ||
            TAV_READER[2] != 0x8A40) {
            g_tav_err = 1;
            ERR("tav: AE table or reader not stock, not patching");
            return;
        }
        g_tav_stock = *TAV_LIMIT;
        g_tav_have = 1;
        DBG_HEX("tav: stock limit", g_tav_stock);
    }
    uint32_t sv = GR2_FN_PROP_GET(GR2_PROPID_ISO_AUTO_SV_MAX);   /* hooked: 1 EV rounding as AUTO-HI */
    if (sv < 0x120 || sv > 0x270) return;
    uint16_t agc = (uint16_t)(((sv * 0xAAABu) >> 17) - 0x50);   /* sv / 3, exact below 0x18000 */
    if (*TAV_LIMIT != agc) {
        *TAV_LIMIT = agc;
        DBG_HEX2("tav: limit (Sv, agc)", sv, agc);
    }
    g_tav_on = 1;
}

/* ---- Auto ISO: slowest shutter and max ISO for AUTO (finding 21) --------------
 * The AE re-reads its AE_F tuning table at every iteration:
 *   +0xBE lo_tv_wide_stb_off  shutter where AUTO ISO starts rising (16/stop,
 *                             0 = 1 s; stock 0x55 = 1/40), also AUTO-HI when
 *                             "Change Shutter Speed" is Auto
 *   +0xC4 / +0xC2             AUTO ceiling flash off / on (agc, stock 0x30 = ISO 800)
 *   +0x2C8                    AUTO ceiling with DR compensation on (stock 0x50 = 3200)
 *   +0xCA / +0xCC             slowest shutter once ISO is at its ceiling (stock 0 =
 *                             1 s / -48 = 8 s). The AE takes P+0x12 from +0xCA or,
 *                             depending on a caller flag (FUN_a01f8bbc r2 / g+0xE7),
 *                             from +0xCC, which FUN_a01f9ec4 also returns as the
 *                             flash-off slowest Tv. "+0xCA only" still let P slow
 *                             down on hardware (2026-11-01), so "hold" sets both to
 *                             the slowest shutter above: P and Av underexpose
 *                             instead of going slower.
 * Stock values come back in Movie mode and on STOCK. "1/60 + crop" follows the
 * crop setting: 1/60 at 28 mm, 1/80 at 35 mm, 1/100 at 47 mm. */
#define AE_READER_A    ((volatile uint16_t *)0xA01F80C0)   /* 30A0 8BC0 71B0: S+6 = T+0xBE */
#define AE_READER_B    ((volatile uint16_t *)0xA01F811E)   /* 30C0 8880 8360: L+0x1A = T+0xC4 */
#define AE_READER_C    ((volatile uint16_t *)0xA01F80F8)   /* 30C0 8840 82A0: L+0x14 = T+0xC2 */
#define PROPID_CROP    0x021                               /* 0 off, 1 35 mm, 2 47 mm */
#define MODE_MOVIE     7
static const uint16_t AE_OFF[6]   = { 0xBE, 0xC4, 0xC2, 0x2C8, 0xCA, 0xCC };
static const uint16_t AE_STOCK[6] = { 0x55, 0x30, 0x30, 0x50, 0x00, 0xFFD0 };
#define AEF16(o)       (*(volatile uint16_t *)(AEF_TABLE + (o)))

static int g_ae_err, g_ae_on;

static int ae_guard(void) {
    static const uint16_t RA[3] = { 0x30A0, 0x8BC0, 0x71B0 }, RB[3] = { 0x30C0, 0x8880, 0x8360 },
                          RC[3] = { 0x30C0, 0x8840, 0x82A0 };
    if (*AEF_TABLE_LIT != AEF_TABLE) return 0;
    for (int i = 0; i < 3; i++)
        if (AE_READER_A[i] != RA[i] || AE_READER_B[i] != RB[i] || AE_READER_C[i] != RC[i]) return 0;
    for (int i = 0; i < 6; i++) if (AEF16(AE_OFF[i]) != AE_STOCK[i]) return 0;
    return 1;
}

static void ae_write(int i, uint16_t v) {
    if (AEF16(AE_OFF[i]) != v) AEF16(AE_OFF[i]) = v;
}

static void aiso_update(void) {
    int min = g_val[S_AISOMIN], max = g_val[S_AISOMAX], hold = g_val[S_AISOHOLD];
    if (GR2_FN_PROP_GET_RAW(GR2_PROPID_SHOOTING_MODE) == MODE_MOVIE) min = max = hold = 0;
    if (!min && !max && !hold && !g_ae_on) return;
    if (g_ae_err) return;
    if (!g_ae_on) {
        if (!ae_guard()) {
            g_ae_err = 1;
            ERR("aiso: AE table or readers not stock, not patching");
            return;
        }
        g_ae_on = 1;
    }
    uint32_t tv = 0;                                           /* prop units: 48 per stop */
    if (min == 1) {
        uint32_t crop = GR2_FN_PROP_GET(PROPID_CROP);
        tv = 0x120 + (crop == 1 ? 16 : crop == 2 ? 32 : 0);   /* 1/60, 1/80, 1/100 */
    } else if (min > 1) {
        tv = tv_value(11 + (min - 2));                         /* TV_TXT[11..23]: 1/15 .. 1/250 */
    }
    uint16_t tv16 = min ? (uint16_t)((tv * 0xAAABu) >> 17) : AE_STOCK[0];   /* tv / 3 */
    ae_write(0, tv16);
    uint16_t agc = max ? (uint16_t)(16 + 16 * max) : 0;        /* 1 = ISO 400 (32) .. 7 = 25600 (128) */
    for (int i = 1; i < 4; i++) ae_write(i, max ? agc : AE_STOCK[i]);
    ae_write(4, hold ? tv16 : AE_STOCK[4]);
    ae_write(5, hold ? tv16 : AE_STOCK[5]);
    if (!min && !max && !hold) {
        g_ae_on = 0;
        DBG("aiso: stock AE limits");
    }
}

/* ---- Snap distances 0.7 m / 1.2 m (finding 18 §4) -------------------------------
 * ON makes the snap choices "1m" and "1.5m" focus at 0.7 m and 1.2 m (common
 * hyperfocal-style distances; a free choice per snap was tried and dropped after
 * the 2026-11-01 hardware test). LData = 1024 / metres. Lens_F snap table and
 * AF_F fullPressSnapLdata are read live at half/full press; start-up pre-focus
 * and OSD distance code hold the values as literals / immediates: `movs rX,#1;
 * lsls rX,#10` becomes `movs rX,#n; lsls rX,#3` (LData = 8 n), and OSD path 1's
 * 1.5 m `ldr r6,=0x2AA` becomes `movs r6,#n` branching into the 1 m path's
 * `lsls #3`. Patches go in table order, undo in reverse; each intermediate state
 * is a legal (clamped) distance. Labels follow the language table. RAM only. */
#define SNAP_L1      1464     /* 0.7 m: 1024 / 0.7, multiple of 8 */
#define SNAP_L15     856      /* 1.2 m */
static const uint16_t SNAP_LABEL1[] = { '0', '.', '7', 'm', 0 };    /* stock style ("1.5m") */
static const uint16_t SNAP_LABEL15[] = { '1', '.', '2', 'm', 0 };

typedef struct { uint32_t addr; uint8_t width, group, kind; uint32_t stock; uint32_t cfw; } mpatch_t;
enum { P_LDATA, P_MOVS, P_FIXED };   /* value = LData / movs base | LData >> 3 / fixed cfw */

static const mpatch_t SNAP[] = {     /* group 15 takes the 1.2 m value, the others the 0.7 m value */
    { 0xA06FD5E2u, 2, 1,  P_LDATA, 0x0400, 0 },        /* Lens_F snap[1]  (prop 1, 1 m)   */
    { 0xA06FD5E8u, 2, 15, P_LDATA, 0x02AA, 0 },        /* Lens_F snap[4]  (prop 2, 1.5 m) */
    { 0xA0700066u, 2, 1,  P_LDATA, 0x0400, 0 },        /* AF_F fullPressSnapLdata[0]      */
    { 0xA070006Cu, 2, 15, P_LDATA, 0x02AA, 0 },        /* AF_F fullPressSnapLdata[3]      */
    { 0xA015F3ECu, 4, 15, P_LDATA, 0x02AA, 0 },        /* start-up 1.5 m literal (one user) */
    { 0xA01BE978u, 4, 15, P_LDATA, 0x02AA, 0 },        /* OSD path 2 1.5 m literal (one user) */
    { 0xA015F19Cu, 2, 1,  P_FIXED, 0x0280, 0x00C0 },   /* start-up 1 m: lsls r0,#10 -> #3 */
    { 0xA015F19Au, 2, 1,  P_MOVS,  0x2001, 0x2000 },   /*               movs r0,#1 -> #n */
    { 0xA01BE584u, 2, 1,  P_FIXED, 0x02A4, 0x00E4 },   /* OSD path 2 1 m: lsls r4,#10 -> #3 */
    { 0xA01BE582u, 2, 1,  P_MOVS,  0x2401, 0x2400 },   /*                 movs r4,#1 -> #n */
    { 0xA01BD75Cu, 2, 0,  P_FIXED, 0x02B6, 0x00F6 },   /* OSD path 1 1 m: lsls r6,#10 -> #3 (shared) */
    { 0xA01BD75Au, 2, 0,  P_MOVS,  0x2601, 0x2600 },   /*                 movs r6,#1 -> #n (1 m value) */
    { 0xA01BD760u, 2, 15, P_MOVS,  0x4E1B, 0x2600 },   /* OSD path 1 1.5 m: ldr r6,=0x2AA -> movs r6,#n */
    { 0xA01BD762u, 2, 15, P_FIXED, 0xE7CD, 0xE7FB },   /*   b 0xA01BD700 -> b 0xA01BD75C (lsls #3) */
};
#define N_SNAP ((int)(sizeof(SNAP) / sizeof(SNAP[0])))

#define STRID_SNAP_1M   0x42
#define STRID_SNAP_15M  0x43

static int g_snap_on, g_snap_err;
static volatile uint32_t *g_snap_strtab;
static uint32_t g_snap_old1, g_snap_old15;

static uint32_t mp_read(const mpatch_t *p) {
    return p->width == 4 ? *(volatile uint32_t *)p->addr : *(volatile uint16_t *)p->addr;
}

static void mp_write(const mpatch_t *p, uint32_t v) {
    if (p->width == 4) *(volatile uint32_t *)p->addr = v;
    else *(volatile uint16_t *)p->addr = (uint16_t)v;
}

static uint32_t mp_value(const mpatch_t *p) {
    uint32_t l = p->group == 15 ? SNAP_L15 : SNAP_L1;
    if (p->kind == P_LDATA) return l;
    if (p->kind == P_MOVS) return p->cfw | (l >> 3);
    return p->cfw;
}

static int ustr_is(uint32_t ptr, const char *s) {
    const volatile uint16_t *u = (const volatile uint16_t *)ptr;
    if (ptr == 0) return 0;
    while (*s && *u == (uint16_t)*s) { u++; s++; }
    return *s == 0 && *u == 0;
}

static void snap_labels(int on) {
    if (g_snap_strtab) {                       /* back to the stock strings first */
        g_snap_strtab[STRID_SNAP_1M - 1] = g_snap_old1;
        g_snap_strtab[STRID_SNAP_15M - 1] = g_snap_old15;
        g_snap_strtab = 0;
    }
    if (!on) return;
    uint32_t lang = GR2_FN_PROP_GET(GR2_PROPID_LANGUAGE);
    if (lang > 20) return;
    volatile uint32_t *t = (volatile uint32_t *)GR2_LANG_TABLES[lang];
    if (!ustr_is(t[STRID_SNAP_1M - 1], "1m") || !ustr_is(t[STRID_SNAP_15M - 1], "1.5m")) {
        DBG("snap: labels not 1m/1.5m in this language, keeping them");
        return;
    }
    g_snap_strtab = t;
    g_snap_old1 = t[STRID_SNAP_1M - 1];
    g_snap_old15 = t[STRID_SNAP_15M - 1];
    t[STRID_SNAP_1M - 1] = (uint32_t)SNAP_LABEL1;
    t[STRID_SNAP_15M - 1] = (uint32_t)SNAP_LABEL15;
}

static int g_labels_ok;    /* the menus are installed: their language tables may carry our labels */

static void snap_update(void) {
    int want = g_val[S_SNAP];
    if (g_snap_err || want == g_snap_on) return;
    if (!want) {
        snap_labels(0);
        for (int i = N_SNAP - 1; i >= 0; i--)
            if (mp_read(&SNAP[i]) == mp_value(&SNAP[i])) mp_write(&SNAP[i], SNAP[i].stock);
        g_snap_on = 0;
        DBG("snap: stock distances");
        return;
    }
    for (int i = 0; i < N_SNAP; i++) {
        uint32_t v = mp_read(&SNAP[i]);
        if (v != SNAP[i].stock) {
            ERR_HEX2("snap: not stock, not patching (addr, found)", SNAP[i].addr, v);
            g_snap_err = 1;
            return;
        }
    }
    for (int i = 0; i < N_SNAP; i++) mp_write(&SNAP[i], mp_value(&SNAP[i]));
    g_snap_on = 1;
    if (g_labels_ok) snap_labels(1);
    DBG_HEX2("snap: LData now (1m, 1.5m)", SNAP_L1, SNAP_L15);
}

/* ---- SV getter hook (finding 21 R3, R5) -----------------------------------------
 * The SV getter hook (prop 0x110) rewrites the ISO setting per shooting mode:
 * TAv sees AUTO unless the setting is AUTO-HI, M sees ISO 100 for AUTO and
 * AUTO-HI. Each option changes one movs halfword; neighbouring halfwords are
 * checked as guards.
 *  - TAv min: in TAv plain AUTO is seen as AUTO-HI, so the Auto-Hi minimum
 *    (and maximum) apply.
 *  - M auto: M sees AUTO, so the AE engine's M + auto-ISO branch solves Sv
 *    between the floor and AE_F +0xC4 (= "Auto ISO max"; ISO 800 if STOCK).
 *    (Exposure compensation in M via the EV-comp getter, R5b, was dropped
 *    after the 2026-11-01 hardware test.)
 * The raw ISO property is untouched, so nothing persists. A change reaches
 * the AE snapshot at the next settings or mode event (entering M/TAv). */
typedef struct { uint32_t addr; uint16_t stock, cfw; } hpatch_t;   /* stock == cfw: guard only */

static const hpatch_t HP_TAVMIN[] = {
    { 0xA00DC22Eu, 0x4284, 0x4284 },   /* TAv: cmp r4,r0 (setting == AUTO-HI?) */
    { 0xA00DC232u, 0x0002, 0x0002 },   /*      movs r2,r0 (AUTO-HI) */
    { 0xA00DC236u, 0x000A, 0x0002 },   /*      movs r2,r1 (AUTO) -> movs r2,r0 (AUTO-HI) */
};
static const hpatch_t HP_MAUTO[] = {
    { 0xA00DC206u, 0x1E81, 0x1E81 },   /* subs r1,r0,#2: r1 = AUTO */
    { 0xA00DC214u, 0x2D06, 0x2D06 },   /* cmp r5,#6 (M) */
    { 0xA00DC22Au, 0x22F0, 0x000A },   /* M: movs r2,#0xF0 (ISO 100) -> movs r2,r1 (AUTO) */
};
#define SV_LIT_AUTOHI  (*(volatile uint32_t *)0xA00DC3C4)   /* r0 = 0x989813 in the SV hook */
#define SV_AUTO_HI     0x989813u                             /* ISO AUTO-HI (AUTO = 0x989811) */
#define AEF_FUNCTION_F 0x138                                  /* bit 0: M auto ISO enabled */
#define NHP(t) ((int)(sizeof(t) / sizeof((t)[0])))

/* 1 if every halfword holds its stock (on = 0) or CFW (on = 1) value. */
static int hp_is(const hpatch_t *p, int n, int on) {
    for (int i = 0; i < n; i++)
        if (*(volatile uint16_t *)p[i].addr != (on ? p[i].cfw : p[i].stock)) return 0;
    return 1;
}

/* One aligned halfword store per changed instruction: in table order on, reverse off. */
static void hp_set(const hpatch_t *p, int n, int on) {
    for (int k = 0; k < n; k++) {
        const hpatch_t *e = &p[on ? k : n - 1 - k];
        if (e->cfw != e->stock) *(volatile uint16_t *)e->addr = on ? e->cfw : e->stock;
    }
}

static uint8_t g_tavmin_on, g_mauto_on, g_hook_err;

/* Switch one option; returns the new state (unchanged and g_hook_err set when not stock). */
static uint8_t hp_switch(const hpatch_t *p, int n, uint8_t cur, int want, const char *what) {
    if (!want == !cur) return cur;
    if (!hp_is(p, n, cur)) {
        ERR_HEX2("ae hooks: code not as expected, not patching (option, first addr)", (uint32_t)what[0], p[0].addr);
        g_hook_err = 1;
        return cur;
    }
    hp_set(p, n, want);
    DBG_HEX2("ae hooks: option set (option, on)", (uint32_t)what[0], (uint32_t)want);
    return (uint8_t)(want != 0);
}

static void hooks_update(void) {
    if (g_hook_err) return;
    int mauto = g_val[S_MAUTO];
    if (mauto && !g_mauto_on && (SV_LIT_AUTOHI != SV_AUTO_HI || !(AEF16(AEF_FUNCTION_F) & 1))) {
        ERR("ae hooks: M auto-ISO engine support not as expected, not patching");
        g_hook_err = 1;
        return;
    }
    g_tavmin_on = hp_switch(HP_TAVMIN, NHP(HP_TAVMIN), g_tavmin_on, g_val[S_TAVMIN], "T");
    g_mauto_on = hp_switch(HP_MAUTO, NHP(HP_MAUTO), g_mauto_on, mauto != 0, "M");
}

/* ---- Engine ------------------------------------------------------------------ */

static uint32_t g_ctx;
static int g_reapply;      /* polls left until the post-change re-apply */
#define REAPPLY_POLLS 5    /* ~1 s at 200 ms */

static uint32_t context(void) {
    return GR2_FN_PROP_GET_RAW(GR2_PROPID_SHOOTING_MODE) |
           (GR2_FN_PROP_GET_RAW(GR2_PROPID_MYSETTING) << 16);
}

static void apply_props(void) {
    clarity_update();
    aisotv_update();
    nr_update();
}

/* Values and recorded stock values from the loaded config. */
static void load_values(void) {
    for (int i = 0; i < S_COUNT; i++) {
        uint32_t v = cfg_get(DESC[i].key, DESC[i].def);
        g_val[i] = (uint8_t)(v < DESC[i].count ? v : DESC[i].def);
    }
    nr_order(S_NRW);
    for (int i = 0; i < N_OVRS; i++) {
        OVRS[i]->has_stock = (uint8_t)cfg_has(OVRS[i]->stock_key);
        OVRS[i]->stock = cfg_get(OVRS[i]->stock_key, 0);
    }
}

static void apply_all(void) {
    g_ctx = context();
    apply_props();
    tav_update();
    aiso_update();
    snap_update();
    hooks_update();
    overlay_set_frame(g_val[S_FRAMES]);
    overlay_set_zebra(g_val[S_ZEBRA]);
}

static int g_early;

/* Flashed loader, boot task, at the IQ-ready call (finding 35): the config is loaded, the mode/MY props are final
 * and AE_F/AF_F/Lens_F are in RAM; live view, the first AE run and the AE snapshot come later. No file writes here:
 * the recorded stock values are saved by settings_init in the task. */
void settings_early(void) {
    load_values();
    apply_all();
    g_early = 1;
    DBG("settings: applied at boot");
}

void settings_labels(void) {
    g_labels_ok = 1;
    if (g_snap_on) snap_labels(1);
}

/* The task's late phase. Without settings_early (Startup.ttl path; rom: no valid config on D: at the first hook) the
 * config is loaded here, with the card copy as the fallback, and applied now. */
void settings_init(void) {
    if (!g_early) {
        cfg_load(1);
        load_values();
        apply_all();
    }
    settings_labels();
    cfg_save();
    cfg_sync_card();
}

void settings_poll(void) {
    uint32_t ctx = context();
    if (ctx != g_ctx) {
        g_ctx = ctx;
        g_reapply = REAPPLY_POLLS;
    } else if (g_reapply && --g_reapply == 0) {
        DBG_HEX("set: context changed, re-applying (mode | MY << 16)", ctx);
        apply_props();
        cfg_save();
    }
    tav_update();
    aiso_update();
}

int settings_count(void) {
    return S_COUNT;
}

int settings_at(int k) {
    return ORDER[k];
}

int settings_is_toggle(int i) {
    return DESC[i].count == 2;
}

int settings_tab_count(void) {
    return TAB_COUNT;
}

const char *settings_tab_name(int tab) {
    return TAB_NAME[tab];
}

int settings_tab(int i) {
    return DESC[i].tab;
}

const char *settings_label(int i) {
    return DESC[i].label;
}

const char *settings_value(int i) {
    static char digit[2];
    int v = g_val[i];
    switch (i) {
    case S_TAV:     return g_tav_err ? "ERR" : (v ? "ON" : "OFF");
    case S_SNAP:    return g_snap_err ? "ERR" : v ? "0.7m + 1.2m" : "STOCK";
    case S_FRAMES: {
        static const char *const FRAME_TXT[FRAME_COUNT] = { "OFF", "XPan 65:24", "6x7", "4:5", "1:1", "16:9" };
        return FRAME_TXT[v];
    }
    case S_ZEBRA: {
        static const char *const ZEBRA_TXT[4] = { "OFF", "Y 235+", "Y 245+", "Y 252+" };
        return ZEBRA_TXT[v];
    }
    case S_CLARITY:
        if (!v) return "STOCK";
        digit[0] = (char)('0' + v);
        digit[1] = 0;
        return digit;
    case S_AISOTV:  return TV_TXT[v];
    case S_AISOMIN: return v == 0 ? "STOCK" : v == 1 ? "1/60 + crop" : TV_TXT[11 + (v - 2)];
    case S_AISOMAX: {
        static const char *const MAX_TXT[8] = { "STOCK", "ISO 400", "ISO 800", "ISO 1600", "ISO 3200",
                                                "ISO 6400", "ISO 12800", "ISO 25600" };
        return MAX_TXT[v];
    }
    case S_AISOHOLD: return v ? "KEEP SHUTTER" : "SLOWER SHUTTER";
    case S_NR:      return v ? "CUSTOM" : "STOCK";
    case S_TAVMIN:  return g_hook_err ? "ERR" : v ? "ON" : "OFF";
    case S_MAUTO:   return g_hook_err ? "ERR" : v ? "ON" : "OFF";
    default:        return ISO_TXT[v];
    }
}

void settings_step(int i, int dir) {
    int n = DESC[i].count;
    int v = g_val[i] + dir;
    if (v < 0) v = n - 1;
    else if (v >= n) v = 0;
    g_val[i] = (uint8_t)v;
    switch (i) {
    case S_TAV:     tav_update(); break;
    case S_SNAP:    snap_update(); break;
    case S_FRAMES:  overlay_set_frame(v); break;
    case S_ZEBRA:   overlay_set_zebra(v); break;
    case S_CLARITY: clarity_update(); break;
    case S_AISOTV:  aisotv_update(); break;
    case S_AISOMIN:
    case S_AISOMAX:
    case S_AISOHOLD: aiso_update(); break;
    case S_TAVMIN:
    case S_MAUTO:   hooks_update(); break;
    default:
        nr_order(i);
        nr_update();
        cfg_set(DESC[S_NRW].key, g_val[S_NRW]);
        cfg_set(DESC[S_NRM].key, g_val[S_NRM]);
        cfg_set(DESC[S_NRS].key, g_val[S_NRS]);
        break;
    }
    cfg_set(DESC[i].key, g_val[i]);
}

void settings_save(void) {
    cfg_save();
}

#if CFW_DEBUG
void settings_log_status(void) {
    for (int i = 0; i < S_COUNT; i++) {
        char line[72], *p = line;
        p = put_str(p, "status: ");
        p = put_str(p, DESC[i].label);
        p = put_str(p, " = ");
        p = put_str(p, settings_value(i));
        *p = 0;
        cfw_log(line);
    }
    cfw_log_hex("status: prop COLOR_MODE (0x2D)", GR2_FN_PROP_GET(GR2_PROPID_COLOR_MODE));
    cfw_log_hex("status: prop CLARITY_16 (0xCE)", GR2_FN_PROP_GET_RAW(GR2_PROPID_CLARITY_16));
    cfw_log_hex("status: prop ISO_AUTO_SV_MAX (0x111)", GR2_FN_PROP_GET(GR2_PROPID_ISO_AUTO_SV_MAX));
    cfw_log_hex("status: prop ISO_AUTO_CHANGE_TV (0x113)", GR2_FN_PROP_GET_RAW(GR2_PROPID_ISO_AUTO_CHANGE_TV));
    cfw_log_hex("status: prop NOISE_REDUCTION (0xEF)", GR2_FN_PROP_GET_RAW(GR2_PROPID_NOISE_REDUCTION));
    cfw_log_hex("status: TAv AE limit (agc)", *TAV_LIMIT);
    cfw_log_hex2("status: AUTO ISO slowest Tv, max agc", AEF16(0xBE), AEF16(0xC4));
    cfw_log_hex2("status: slowest Tv at max ISO, P / Av (AE_F+0xCA, +0xCC)", AEF16(0xCA), AEF16(0xCC));
}
#endif
