#ifndef GR2_UI_H
#define GR2_UI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SLOT_POSI = 0,      /* Positive Film slot */
    SLOT_BLEACH,        /* Bleach Bypass slot */
    SLOT_RETRO,         /* Retro slot */
    SLOT_CROSS,         /* Cross Process slot */
    SLOT_MY1,           /* Dial MY1 */
    SLOT_MY2,           /* Dial MY2 */
    SLOT_MY3,           /* Dial MY3 */
    SLOT_COUNT
} AssignableSlot;

typedef enum {
    TAB_FILMS = 0,      /* Film simulation catalog & slot assignment */
    TAB_SETTINGS,       /* Custom features & toggle switches */
    TAB_COUNT
} MenuTab;

typedef struct {
    /* Feature Toggles (All FALSE / Stock by default) */
    bool clarity_slider;        /* In-Camera Clarity slider (patches Vignetting label) */
    bool scurve_contrast;       /* Smart photographic S-curve */
    bool auto_iso_floor;        /* 1/125s shutter speed floor */
    bool fine_grain_nr;         /* Fine-grain noise reduction thresholds */
    bool xpan_guidelines;       /* 65:24 cinematic frame lines */

    /* Active Film Assignment */
    int selected_film_idx;      /* Index into g_film_catalog */
    AssignableSlot target_slot; /* Destination slot to override */
} CfwSettings;

typedef struct {
    bool menu_visible;
    MenuTab current_tab;
    int current_item_idx;
    CfwSettings settings;
} CfwState;

extern CfwState g_cfw_state;

/* UI Lifecycle & Events */
void ui_init(void);
void ui_render(void);
void ui_toggle_menu(void);
void ui_nav_up(void);
void ui_nav_down(void);
void ui_nav_left(void);
void ui_nav_right(void);
void ui_action_ok(void);

#endif /* GR2_UI_H */
