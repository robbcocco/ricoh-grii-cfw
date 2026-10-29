#include "gr2_ui.h"
#include "films.h"
#include "gr2_hw.h"

void hooks_set_clarity_slider(bool enable);
void hooks_set_scurve_contrast(bool enable);
void hooks_set_auto_iso_floor(bool enable);
void hooks_set_fine_grain_nr(bool enable);
void hooks_patch_slot_name(AssignableSlot slot, const char *film_name);

static inline char *put_int(char *p, int val) {
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

static void render_toggle(char *buf, int idx, int cur_idx, int y, const char *label, bool state) {
    char *p = buf;
    const char *pfx = "gprint 80 ";
    while (*pfx) *p++ = *pfx++;
    p = put_int(p, y);
    *p++ = ' '; *p++ = '"';
    if (cur_idx == idx) {
        *p++ = '>'; *p++ = ' ';
    } else {
        *p++ = ' '; *p++ = ' ';
    }
    while (*label) *p++ = *label++;
    const char *st = state ? " [ON]\"" : " [OFF]\"";
    while (*st) *p++ = *st++;
    *p = '\0';
    GR2_FN_DISPATCH_CMD(buf);
}

CfwState g_cfw_state;
static const char *slot_to_name(AssignableSlot s) {
    switch (s) {
        case SLOT_POSI:   return "Positive Film";
        case SLOT_BLEACH: return "Bleach Bypass";
        case SLOT_RETRO:  return "Retro";
        case SLOT_CROSS:  return "Cross Process";
        case SLOT_MY1:    return "Dial MY1";
        case SLOT_MY2:    return "Dial MY2";
        case SLOT_MY3:    return "Dial MY3";
        default:          return "Unknown";
    }
}

void ui_init(void) {
    g_cfw_state.menu_visible = false;
    g_cfw_state.current_tab = TAB_FILMS;
    g_cfw_state.current_item_idx = 0;

    /* ALL features start OFF / Stock by default */
    g_cfw_state.settings.clarity_slider = false;
    g_cfw_state.settings.scurve_contrast = false;
    g_cfw_state.settings.auto_iso_floor = false;
    g_cfw_state.settings.fine_grain_nr = false;
    g_cfw_state.settings.xpan_guidelines = false;
    g_cfw_state.settings.selected_film_idx = 0;
    g_cfw_state.settings.target_slot = SLOT_POSI;
}

void cfw_log(const char *msg);

void ui_toggle_menu(void) {
    g_cfw_state.menu_visible = !g_cfw_state.menu_visible;
    if (g_cfw_state.menu_visible) {
        cfw_log("--> UI: ui_toggle_menu() OPENED!\n");
    } else {
        cfw_log("--> UI: ui_toggle_menu() CLOSED!\n");
    }
    ui_render();
}

void ui_nav_up(void) {
    if (!g_cfw_state.menu_visible) return;
    int max_items = (g_cfw_state.current_tab == TAB_FILMS) ? 3 : 5;
    g_cfw_state.current_item_idx--;
    if (g_cfw_state.current_item_idx < 0) {
        g_cfw_state.current_item_idx = max_items - 1;
    }
    ui_render();
}

void ui_nav_down(void) {
    if (!g_cfw_state.menu_visible) return;
    int max_items = (g_cfw_state.current_tab == TAB_FILMS) ? 3 : 5;
    g_cfw_state.current_item_idx++;
    if (g_cfw_state.current_item_idx >= max_items) {
        g_cfw_state.current_item_idx = 0;
    }
    ui_render();
}

void ui_nav_left(void) {
    if (!g_cfw_state.menu_visible) return;
    if (g_cfw_state.current_tab == TAB_FILMS) {
        if (g_cfw_state.current_item_idx == 0) {
            g_cfw_state.settings.selected_film_idx--;
            if (g_cfw_state.settings.selected_film_idx < 0) {
                g_cfw_state.settings.selected_film_idx = FILM_CATALOG_SIZE - 1;
            }
        } else if (g_cfw_state.current_item_idx == 1) {
            int slot = (int)g_cfw_state.settings.target_slot - 1;
            if (slot < 0) slot = SLOT_COUNT - 1;
            g_cfw_state.settings.target_slot = (AssignableSlot)slot;
        }
    } else {
        ui_action_ok();
        return;
    }
    ui_render();
}

void ui_nav_right(void) {
    if (!g_cfw_state.menu_visible) return;
    if (g_cfw_state.current_tab == TAB_FILMS) {
        if (g_cfw_state.current_item_idx == 0) {
            g_cfw_state.settings.selected_film_idx++;
            if (g_cfw_state.settings.selected_film_idx >= FILM_CATALOG_SIZE) {
                g_cfw_state.settings.selected_film_idx = 0;
            }
        } else if (g_cfw_state.current_item_idx == 1) {
            int slot = (int)g_cfw_state.settings.target_slot + 1;
            if (slot >= SLOT_COUNT) slot = 0;
            g_cfw_state.settings.target_slot = (AssignableSlot)slot;
        }
    } else {
        ui_action_ok();
        return;
    }
    ui_render();
}

void ui_action_ok(void) {
    if (!g_cfw_state.menu_visible) return;

    if (g_cfw_state.current_tab == TAB_FILMS) {
        if (g_cfw_state.current_item_idx == 2) {
            /* Apply active film to active slot */
            const FilmProfile *film = &g_film_catalog[g_cfw_state.settings.selected_film_idx];
            film_apply(film, NULL);
            hooks_patch_slot_name(g_cfw_state.settings.target_slot, film->name);
            ui_toggle_menu();
            return;
        }
    } else {
        /* Toggle selected setting item */
        switch (g_cfw_state.current_item_idx) {
            case 0:
                g_cfw_state.settings.clarity_slider = !g_cfw_state.settings.clarity_slider;
                hooks_set_clarity_slider(g_cfw_state.settings.clarity_slider);
                break;
            case 1:
                g_cfw_state.settings.scurve_contrast = !g_cfw_state.settings.scurve_contrast;
                hooks_set_scurve_contrast(g_cfw_state.settings.scurve_contrast);
                break;
            case 2:
                g_cfw_state.settings.auto_iso_floor = !g_cfw_state.settings.auto_iso_floor;
                hooks_set_auto_iso_floor(g_cfw_state.settings.auto_iso_floor);
                break;
            case 3:
                g_cfw_state.settings.fine_grain_nr = !g_cfw_state.settings.fine_grain_nr;
                hooks_set_fine_grain_nr(g_cfw_state.settings.fine_grain_nr);
                break;
            case 4:
                g_cfw_state.settings.xpan_guidelines = !g_cfw_state.settings.xpan_guidelines;
                if (g_cfw_state.settings.xpan_guidelines) {
                    GR2_FN_DISPATCH_CMD("gline 0 122 640 122");
                    GR2_FN_DISPATCH_CMD("gline 0 358 640 358");
                } else {
                    GR2_FN_DISPATCH_CMD("greset");
                }
                break;
        }
    }
    ui_render();
}

void ui_render(void) {
    if (!g_cfw_state.menu_visible) {
        GR2_FN_DISPATCH_CMD("greset");
        return;
    }

    /* 1. Setup Graphics View */
    GR2_FN_DISPATCH_CMD("gdevice lcd");
    GR2_FN_DISPATCH_CMD("gpen white black");
    GR2_FN_DISPATCH_CMD("grect 50 30 590 450");

    /* 2. Title & Tab Bar */
    if (g_cfw_state.current_tab == TAB_FILMS) {
        GR2_FN_DISPATCH_CMD("gprint 70 50 \"[* FILM RECIPES *]      [SETTINGS]\"");
    } else {
        GR2_FN_DISPATCH_CMD("gprint 70 50 \" [FILM RECIPES]      [* SETTINGS *]\"");
    }
    GR2_FN_DISPATCH_CMD("gline 70 75 570 75");

    char buf[128];

    /* 3. Render Active Tab Content */
    if (g_cfw_state.current_tab == TAB_FILMS) {
        const FilmProfile *film = &g_film_catalog[g_cfw_state.settings.selected_film_idx];

        /* Row 0: Film Profile Selection */
        char *p = buf;
        const char *p0 = (g_cfw_state.current_item_idx == 0) ? "gprint 80 120 \"> Film Look: < " : "gprint 80 120 \"  Film Look: < ";
        while (*p0) *p++ = *p0++;
        const char *fn = film->name;
        while (*fn) *p++ = *fn++;
        const char *p0e = " >\"";
        while (*p0e) *p++ = *p0e++;
        *p = '\0';
        GR2_FN_DISPATCH_CMD(buf);
        /* Row 1: Target Slot Selection */
        p = buf;
        const char *p1 = (g_cfw_state.current_item_idx == 1) ? "gprint 80 170 \"> Assign To: < " : "gprint 80 170 \"  Assign To: < ";
        while (*p1) *p++ = *p1++;
        const char *sn = slot_to_name(g_cfw_state.settings.target_slot);
        while (*sn) *p++ = *sn++;
        const char *p1e = " >\"";
        while (*p1e) *p++ = *p1e++;
        *p = '\0';
        GR2_FN_DISPATCH_CMD(buf);

        /* Row 2: Action Button */
        if (g_cfw_state.current_item_idx == 2) {
            GR2_FN_DISPATCH_CMD("gprint 80 230 \"> [ APPLY FILM TO SLOT (OK) ]\"");
        } else {
            GR2_FN_DISPATCH_CMD("gprint 80 230 \"  [ APPLY FILM TO SLOT ]\"");
        }
    } else {
        /* Tab: Settings Toggles */
        render_toggle(buf, 0, g_cfw_state.current_item_idx, 110, "In-Camera Clarity Slider :", g_cfw_state.settings.clarity_slider);
        render_toggle(buf, 1, g_cfw_state.current_item_idx, 150, "Smart S-Curve Contrast   :", g_cfw_state.settings.scurve_contrast);
        render_toggle(buf, 2, g_cfw_state.current_item_idx, 190, "Auto-ISO 1/125s Floor    :", g_cfw_state.settings.auto_iso_floor);
        render_toggle(buf, 3, g_cfw_state.current_item_idx, 230, "High-ISO Detail NR       :", g_cfw_state.settings.fine_grain_nr);
        render_toggle(buf, 4, g_cfw_state.current_item_idx, 270, "65:24 XPan Frame Lines   :", g_cfw_state.settings.xpan_guidelines);
    }

    /* 4. Footer Help */
    GR2_FN_DISPATCH_CMD("gline 70 380 570 380");
    GR2_FN_DISPATCH_CMD("gprint 80 400 \"[UP/DN] Select   [LT/RT] Change/Toggle   [OK] Action\"");
}
