#include "films.h"
#include "gr2_hw.h"

static inline char *put_int(char *p, int val) {
    if (val < 0) {
        *p++ = '-';
        val = -val;
    }
    if (val >= 100) {
        *p++ = '0' + (val / 100);
        val %= 100;
        *p++ = '0' + (val / 10);
        val %= 10;
    } else if (val >= 10) {
        *p++ = '0' + (val / 10);
        val %= 10;
    }
    *p++ = '0' + val;
    return p;
}

static void simple_sprintf_mycmtx(char *buf, const FilmProfile *f) {
    char *p = buf;
    const char *prefix = "mycmtx ims_set2 (";
    while (*prefix) *p++ = *prefix++;

    p = put_int(p, f->col0[0]); *p++ = ',';
    p = put_int(p, f->col0[1]); *p++ = ',';
    p = put_int(p, f->col0[2]); *p++ = ')'; *p++ = ' '; *p++ = '(';

    p = put_int(p, f->col1[0]); *p++ = ',';
    p = put_int(p, f->col1[1]); *p++ = ',';
    p = put_int(p, f->col1[2]); *p++ = ')'; *p++ = ' '; *p++ = '(';

    p = put_int(p, f->col2[0]); *p++ = ',';
    p = put_int(p, f->col2[1]); *p++ = ',';
    p = put_int(p, f->col2[2]); *p++ = ')';
    *p = '\0';
}
const FilmProfile g_film_catalog[FILM_CATALOG_SIZE] = {
    {
        .id = "portra400",
        .name = "Kodak Portra 400",
        .target_slot = "efc_posiFilm",
        .col0 = {101, -1, -1},
        .col1 = {32, 108, 4},
        .col2 = {-5, 22, 125},
        .dcsve = "65617",
        .tint = "warm"
    },
    {
        .id = "classicneg",
        .name = "Fuji Classic Neg",
        .target_slot = "efc_bleachBypass",
        .col0 = {120, -3, 5},
        .col1 = {-29, 107, 8},
        .col2 = {38, 24, 115},
        .dcsve = "57528",
        .tint = "cool"
    },
    {
        .id = "gold200",
        .name = "Kodak Gold 200",
        .target_slot = "efc_retro",
        .col0 = {104, -1, -2},
        .col1 = {22, 107, 7},
        .col2 = {2, 22, 123},
        .dcsve = "66727",
        .tint = "warm"
    },
    {
        .id = "ektar100",
        .name = "Kodak Ektar 100",
        .target_slot = "efc_posiFilm",
        .col0 = {107, -3, -2},
        .col1 = {27, 108, 3},
        .col2 = {-6, 23, 126},
        .dcsve = "57708",
        .tint = "normal"
    },
    {
        .id = "pro400h",
        .name = "Fuji Pro 400H",
        .target_slot = "efc_crossProcess",
        .col0 = {100, 0, -2},
        .col1 = {33, 120, 9},
        .col2 = {-5, 9, 121},
        .dcsve = "54516",
        .tint = "cool"
    },
    {
        .id = "vision3_250d",
        .name = "Kodak Vision3 250D",
        .target_slot = "efc_bleachBypass",
        .col0 = {117, -2, -3},
        .col1 = {20, 122, -5},
        .col2 = {-9, 7, 136},
        .dcsve = "65618",
        .tint = "normal"
    },
    {
        .id = "trix400",
        .name = "Kodak Tri-X 400",
        .target_slot = "efc_highContrast",
        .col0 = {128, 0, 0},
        .col1 = {0, 128, 0},
        .col2 = {0, 0, 128},
        .dcsve = "79509",
        .tint = "bw"
    },
    {
        .id = "cinestill800t",
        .name = "CineStill 800T",
        .target_slot = "efc_crossProcess",
        .col0 = {95, -5, 8},
        .col1 = {15, 115, 12},
        .col2 = {12, 18, 135},
        .dcsve = "66628",
        .tint = "cool"
    }
};

int film_apply(const FilmProfile *film, const char *slot_override) {
    if (!film) return -1;
    char cmd[128];

    /* 1. Apply Color Matrix */
    simple_sprintf_mycmtx(cmd, film);
    GR2_FN_DISPATCH_CMD(cmd);

    /* 2. Apply Effect Slot Parameters */
    const char *slot = slot_override ? slot_override : film->target_slot;
    char *p = cmd;
    const char *efc_cmd = "efcset ";
    while (*efc_cmd) *p++ = *efc_cmd++;
    while (*slot) *p++ = *slot++;
    *p++ = ' ';
    const char *dcsve = film->dcsve;
    while (*dcsve) *p++ = *dcsve++;
    *p++ = ' ';
    const char *tint = film->tint;
    while (*tint) *p++ = *tint++;
    *p = '\0';

    GR2_FN_DISPATCH_CMD(cmd);
    return 0;
}
