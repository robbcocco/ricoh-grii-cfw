/* Add the choices "CFW Menu" and "Recipes" to "Effect Button Setting"
 * (prop 0x1A6) and "Effect Button Hold Setting" (prop 0x1A7). RAM-only,
 * verify-first (finding 16).
 *
 * Menu records (0x40 B, array 0xA04330D8, index idx): +0x00 title strID,
 * +0x04 prop, +0x0C u16 count1, +0x10 list1 {value, 0, strID} x12 B,
 * +0x14 u16 count2, +0x18 list2 display rows x24 B {strID, 0, 0x04000000, 0,
 * enabled, 1}. Records and lists are read live each time a menu opens; the
 * menu writes list2[i].enabled for every i < count1, so list2 needs >= count1
 * rows and count1 == count2 keeps the lists parallel.
 *
 * Stock handling of the new values (static RE, M2 report): the press dispatch
 * bounds the value (`cmp #0x20; bhs` -> return 0), so 0x20/0x21 on 0x1A6 are a
 * no-op like "Off"; on 0x1A7 any value other than 1 behaves like Wi-Fi.
 * Without the CFW loaded the menus show the first choice.
 *
 * Labels (finding 16 §6): string IDs are bounded by `movs rN,#0x35; lsls rN,#4`
 * (< 0x350) in six accessors, and every ID below 0x350 is in use in some
 * language. Three per-ID tables feed them: the 21 per-language pointer tables
 * (array 0xA05909EC), an 8-byte attribute table (word 0 read by
 * FUN_a03a4ede; 0xFFFFFFFF when out of range -> double-height rows) and a
 * 21-byte per-language font table (FUN_a03a4f18; 0 when out of range -> label
 * not drawn). The CFW copies all three to the heap with IDs up to 0x35F
 * (0x350..0x35F behave as stock: empty string, attribute 0xFFFFFFFF; the two
 * labels take the attributes/fonts of ID 0x1A3, the former "Open CFW" slot),
 * repoints the table array and the two literal-pool base pointers, then raises
 * the six bounds to 0x360 (movs #0x35 -> #0x36, one halfword each).
 *
 * The UI code also checks `id - 1 < 0x34F` itself before calling the
 * accessors (text-object builder FUN_a039d170 and 25 more sites), each with
 * the bound in a literal pool. Without raising those, the hardware showed
 * empty labels and double-height rows. The nine literal-pool words (only used
 * by such checks) become 0x35F: aligned 32-bit data stores, no code patch. */
#include <stdint.h>
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"

#define MENU_REC_BASE   0xA04330D8u
#define MENU_REC(idx)   ((volatile uint8_t *)(MENU_REC_BASE + (idx) * 0x40u))
#define REC_PROP(r)     (*(volatile uint32_t *)((r) + 0x04))
#define REC_COUNT1(r)   (*(volatile uint16_t *)((r) + 0x0C))
#define REC_LIST1(r)    (*(void * volatile *)((r) + 0x10))
#define REC_COUNT2(r)   (*(volatile uint16_t *)((r) + 0x14))
#define REC_LIST2(r)    (*(void * volatile *)((r) + 0x18))

#define IDX_FN1         0xFA
#define IDX_EFX         0xFC
#define IDX_EFX_HOLD    0xFE

/* String tables */
#define N_LANGS         21
#define STR_IDS_STOCK   0x34F          /* IDs 1..0x34F per stock table */
#define STR_IDS_CFW     0x35F          /* IDs 1..0x35F per CFW copy */
#define LANG_TABLE_MIN  0xA04E0000u    /* stock tables: 0xA04EF040..0xA0589CBC */
#define LANG_TABLE_MAX  0xA05A0000u
#define STRID_CFW_MENU  0x354          /* no stock data or code literal holds 0x354 */
#define STRID_RECIPES   0x35E          /* stock uses 0x35E only as PROPID_KEY_OK (0xA0397690) */
#define STOCK_EMPTY_STR 0xA043A354u    /* what the accessors return for IDs >= 0x350 */
#define STRID_TEMPLATE  0x1A3          /* attributes/fonts for the labels (worked for "Open CFW") */

#define ATTR_LIT        ((volatile uint32_t *)0xA03A50A4)   /* literal: 8-byte attribute table base */
#define ATTR_STOCK      0xA058EF74u
#define ATTR_OUT_OF_RANGE 0xFFFFFFFFu
#define FONT_LIT        ((volatile uint32_t *)0xA03A50ACu)  /* literal: 21-byte font table base */
#define FONT_STOCK      0xA058A9F8u
#define FONT_ROW        N_LANGS

#define N_BOUNDS 6
static const uint32_t BOUND_SITE[N_BOUNDS]  = { 0xA03A4AD4u, 0xA03A4B1Cu, 0xA03A4E2Au,
                                                0xA03A4EB2u, 0xA03A4EE6u, 0xA03A4F26u };
static const uint16_t BOUND_STOCK[N_BOUNDS] = { 0x2235, 0x2135, 0x2135,   /* movs rN, #0x35 */
                                                0x2135, 0x2335, 0x2235 };

/* Literal-pool words 0x34F loaded by `ldr rB,=0x34F; subs rT,rId,#1; cmp rT,rB`
 * string-ID checks in the UI (every user of each word is such a check). */
#define N_LIT_BOUNDS 9
static const uint32_t LIT_BOUND[N_LIT_BOUNDS] = {
    0xA039D528u,   /* FUN_a039d170 text object (0xA039D196) */
    0xA039D980u,   /* 0xA039D5FE */
    0xA03ABE78u,   /* 0xA03ABB2E */
    0xA03AEB44u,   /* 0xA03AE904 */
    0xA03BA608u,   /* 0xA03BA20A */
    0xA03D6508u,   /* 0xA03D6482, 0xA03D64BC */
    0xA03D929Cu,   /* 0xA03D912C */
    0xA03E084Cu,   /* 12 checks 0xA03E0462..0xA03E0774 */
    0xA03E0BBCu,   /* 6 checks 0xA03E0880..0xA03E0AA6 */
};
#define LIT_BOUND_STOCK STR_IDS_STOCK
#define LIT_BOUND_CFW   STR_IDS_CFW

typedef struct { uint32_t value, zero, str; } choice_t;                        /* list1 */
typedef struct { uint32_t str, zero, flags, zero2, enabled, one; } row_t;      /* list2 */

#define EFX_STOCK  32
#define HOLD_STOCK 2

/* RAM copies of the stock lists (heap: the CFW window is small). */
typedef struct {
    choice_t efx1[EFX_STOCK + CFW_MENU_COUNT];
    row_t    efx2[EFX_STOCK + CFW_MENU_COUNT];
    row_t    fn1[EFX_STOCK + CFW_MENU_COUNT];
    choice_t hold1[HOLD_STOCK + CFW_MENU_COUNT];
    row_t    hold2[HOLD_STOCK + CFW_MENU_COUNT];
} lists_t;
static lists_t *L;
#define g_efx_list1  (L->efx1)
#define g_efx_list2  (L->efx2)
#define g_fn1_list2  (L->fn1)
#define g_hold_list1 (L->hold1)
#define g_hold_list2 (L->hold2)

/* UTF-16 BE (big-endian CPU: plain uint16_t) */
static const uint16_t LABEL_CFW_MENU[] = { 'C','F','W',' ','M','e','n','u', 0 };
static const uint16_t LABEL_RECIPES[]  = { 'R','e','c','i','p','e','s', 0 };
static const uint32_t MENU_STRID[CFW_MENU_COUNT] = { STRID_CFW_MENU, STRID_RECIPES };

typedef struct {
    uint32_t idx, prop;
    const void *list1, *list2;
} stock_rec_t;

static const stock_rec_t STOCK_FN1  = { IDX_FN1,      0x1A4, (void *)0xA04302E8, (void *)0xA042FFDC };
static const stock_rec_t STOCK_EFX  = { IDX_EFX,      0x1A6, (void *)0xA0430C00, (void *)0xA04308F4 };
static const stock_rec_t STOCK_HOLD = { IDX_EFX_HOLD, 0x1A7, (void *)0xA0431248, (void *)0xA043120C };

static int rec_is_stock(const stock_rec_t *s, uint16_t count) {
    volatile uint8_t *r = MENU_REC(s->idx);
    return REC_PROP(r) == s->prop && REC_COUNT1(r) == count && REC_COUNT2(r) == count &&
           REC_LIST1(r) == s->list1 && REC_LIST2(r) == s->list2;
}

static void copy_words(void *dst, const void *src, uint32_t bytes) {
    uint32_t *d = dst;
    const volatile uint32_t *s = src;
    for (uint32_t i = 0; i < bytes / 4; i++) d[i] = s[i];
}

/* Copy a record's lists, append the CFW choices, then publish: pointers first,
 * counts last, so a menu opening meanwhile sees either stock or complete data. */
static void extend(const stock_rec_t *s, uint16_t count, uint32_t value,
                   choice_t *list1, row_t *list2) {
    volatile uint8_t *r = MENU_REC(s->idx);
    copy_words(list1, s->list1, count * sizeof(choice_t));
    copy_words(list2, s->list2, count * sizeof(row_t));
    for (int m = 0; m < CFW_MENU_COUNT; m++) {
        list1[count + m] = (choice_t){ value + (uint32_t)m, 0, MENU_STRID[m] };
        list2[count + m] = list2[count - 1];
        list2[count + m].str = MENU_STRID[m];
        list2[count + m].enabled = 1;
    }
    REC_LIST1(r) = list1;
    REC_LIST2(r) = list2;
    REC_COUNT1(r) = count + CFW_MENU_COUNT;
    REC_COUNT2(r) = count + CFW_MENU_COUNT;
}

/* Give every language the IDs 0x350..0x35F (see the header comment). */
static int install_labels(void) {
    volatile uint16_t *bound[N_BOUNDS];
    for (int i = 0; i < N_BOUNDS; i++) {
        bound[i] = (volatile uint16_t *)BOUND_SITE[i];
        if (*bound[i] != BOUND_STOCK[i]) {
            ERR_HEX2("menu: string bound not stock (site, found)", BOUND_SITE[i], *bound[i]);
            return -1;
        }
    }
    if (*(const volatile uint16_t *)STOCK_EMPTY_STR != 0 || *ATTR_LIT != ATTR_STOCK ||
        *FONT_LIT != FONT_STOCK) {
        ERR("menu: string attribute/font tables not stock");
        return -1;
    }
    for (int i = 0; i < N_LIT_BOUNDS; i++) {
        uint32_t v = *(const volatile uint32_t *)LIT_BOUND[i];
        if (v != LIT_BOUND_STOCK) {
            ERR_HEX2("menu: string bound literal not stock (addr, found)", LIT_BOUND[i], v);
            return -1;
        }
    }
    uint32_t *lang[N_LANGS];
    for (int l = 0; l < N_LANGS; l++) {
        uint32_t t = GR2_LANG_TABLES[l];
        if (t < LANG_TABLE_MIN || t >= LANG_TABLE_MAX) {
            ERR_HEX2("menu: language table not stock (lang, table)", (uint32_t)l, t);
            return -1;
        }
    }
    /* Allocate everything first: on failure nothing is published (blocks leak). */
    for (int l = 0; l < N_LANGS; l++) lang[l] = GR2_FN_MEMORY_ALLOC(STR_IDS_CFW * 4);
    uint32_t *attr = GR2_FN_MEMORY_ALLOC(STR_IDS_CFW * 8);
    uint8_t *font = GR2_FN_MEMORY_ALLOC((STR_IDS_CFW * FONT_ROW + 3) & ~3u);
    for (int l = 0; l < N_LANGS; l++) if (lang[l] == 0) attr = 0;
    if (attr == 0 || font == 0) {
        ERR("menu: Memory_Alloc failed for the string tables");
        return -1;
    }

    /* Attribute table: 2 words per ID. */
    copy_words(attr, (const void *)ATTR_STOCK, STR_IDS_STOCK * 8);
    const volatile uint32_t *tmpl_attr = (const volatile uint32_t *)(ATTR_STOCK + (STRID_TEMPLATE - 1) * 8);
    for (uint32_t id = STR_IDS_STOCK + 1; id <= STR_IDS_CFW; id++) {
        int label = id == STRID_CFW_MENU || id == STRID_RECIPES;
        attr[(id - 1) * 2] = label ? tmpl_attr[0] : ATTR_OUT_OF_RANGE;
        attr[(id - 1) * 2 + 1] = tmpl_attr[1];
    }
    /* Font table: one signed byte per language per ID. */
    const volatile uint8_t *font_stock = (const volatile uint8_t *)FONT_STOCK;
    for (uint32_t i = 0; i < STR_IDS_STOCK * FONT_ROW; i++) font[i] = font_stock[i];
    for (uint32_t id = STR_IDS_STOCK + 1; id <= STR_IDS_CFW; id++)
        for (int l = 0; l < FONT_ROW; l++)
            font[(id - 1) * FONT_ROW + l] = font_stock[(STRID_TEMPLATE - 1) * FONT_ROW + l];
    /* String pointers. */
    for (int l = 0; l < N_LANGS; l++) {
        uint32_t *c = lang[l];
        copy_words(c, (const void *)GR2_LANG_TABLES[l], STR_IDS_STOCK * 4);
        for (uint32_t id = STR_IDS_STOCK + 1; id <= STR_IDS_CFW; id++) c[id - 1] = STOCK_EMPTY_STR;
        c[STRID_CFW_MENU - 1] = (uint32_t)LABEL_CFW_MENU;
        c[STRID_RECIPES - 1] = (uint32_t)LABEL_RECIPES;
    }

    /* Publish the tables (IDs < 0x350 read identical data), then the bounds. */
    for (int l = 0; l < N_LANGS; l++) GR2_LANG_TABLES[l] = (uint32_t)lang[l];
    *ATTR_LIT = (uint32_t)attr;
    *FONT_LIT = (uint32_t)font;
    for (int i = 0; i < N_BOUNDS; i++) *bound[i] = (uint16_t)(BOUND_STOCK[i] + 1);   /* #0x35 -> #0x36 */
    for (int i = 0; i < N_LIT_BOUNDS; i++) *(volatile uint32_t *)LIT_BOUND[i] = LIT_BOUND_CFW;
    return 0;
}

int cfw_menu_install(void) {
    if (!rec_is_stock(&STOCK_EFX, EFX_STOCK) || !rec_is_stock(&STOCK_HOLD, HOLD_STOCK) ||
        !rec_is_stock(&STOCK_FN1, EFX_STOCK)) {
        ERR("menu: records not stock, not installing");
        return -1;
    }
    L = GR2_FN_MEMORY_ALLOC(sizeof(lists_t));
    if (!L) {
        ERR("menu: Memory_Alloc failed");
        return -1;
    }
    if (install_labels() != 0) return -1;

    /* Fn guide labels for every button come from Fn1's list2 indexed by each
     * button's choice index: give it the extra rows, keep Fn1's own counts. */
    copy_words(g_fn1_list2, STOCK_FN1.list2, EFX_STOCK * sizeof(row_t));
    for (int m = 0; m < CFW_MENU_COUNT; m++) {
        g_fn1_list2[EFX_STOCK + m] = g_fn1_list2[EFX_STOCK - 1];
        g_fn1_list2[EFX_STOCK + m].str = MENU_STRID[m];
    }
    REC_LIST2(MENU_REC(IDX_FN1)) = g_fn1_list2;

    extend(&STOCK_EFX, EFX_STOCK, CFW_EFX_VALUE, g_efx_list1, g_efx_list2);
    extend(&STOCK_HOLD, HOLD_STOCK, CFW_HOLD_VALUE, g_hold_list1, g_hold_list2);
    DBG("menu: CFW Menu / Recipes added (Effect 0x20/0x21, Hold 0x2/0x3)");
    return 0;
}
