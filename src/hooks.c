#include "gr2_hw.h"
#include "gr2_ui.h"

/* Convert ASCII string to Big-Endian UTF-16 in destination buffer */
static void str_to_utf16_be(uint16_t *dest, const char *src, int max_chars) {
    int i = 0;
    while (src[i] && i < max_chars - 1) {
        dest[i] = (uint16_t)((uint8_t)src[i]);
        i++;
    }
    while (i < max_chars) {
        dest[i] = 0;
        i++;
    }
}

/* Native RTOS file logger to H:/cfw_log.txt */
void cfw_log(const char *msg) {
    int fd = GR2_FN_FILE_OPEN("H:/cfw_log.txt", 0x302);
    if (fd != 0) {
        int len = 0;
        while (msg[len]) len++;
        GR2_FN_FILE_WRITE(fd, msg, len);
        GR2_FN_FILE_CLOSE(fd);
    }
}

/* Effect Button Hold Action Hook: ALWAYS ACTIVE (Zero Feature Flag) */
void hook_efx_hold(void) {
    cfw_log("--> Physical Effect button hold detected! Calling ui_toggle_menu()...\n");
    ui_toggle_menu();
}

/* Initialize Custom Firmware Hooks */
void hooks_init(void) {
    /* 1. Unconditionally hook the state machine action vector in DRAM (Thumb mode: bit 0 set) */
    *GR2_ADDR_EFX_HOLD_HOOK = ((uint32_t)hook_efx_hold) | 1;

    /* 2. Patch stock menu text to 'Open Custom OS' so it is always visible in settings */
    str_to_utf16_be(GR2_ADDR_EFX_HOLD_TEXT, "Open Custom OS", 17);
}

/* Feature 1: In-Camera Clarity Slider (Row 4 in Fn sub-menu) */
void hooks_set_clarity_slider(bool enable) {
    if (enable) {
        str_to_utf16_be(GR2_ADDR_NAME_VIGN, "Clarity", 11);
    } else {
        str_to_utf16_be(GR2_ADDR_NAME_VIGN, "Vignetting", 11);
    }
}

/* Feature 2: Smart Photographic S-Curve on Contrast */
void hooks_set_scurve_contrast(bool enable) {
    if (enable) {
        /* D=6 provides extended highlight roll-off shoulder */
        GR2_FN_DISPATCH_CMD("efcset efc_posiFilm 65617 warm");
    } else {
        /* Restore factory Ricoh curve baseline */
        GR2_FN_DISPATCH_CMD("efcset efc_posiFilm 55509 _");
    }
}

/* Feature 3: Auto-ISO Minimum Shutter Speed Floor */
void hooks_set_auto_iso_floor(bool enable) {
    if (enable) {
        /* Minimum 1/125s shutter speed floor */
        GR2_FN_DISPATCH_CMD("svautoset 100 6400 1.125");
    } else {
        /* Restore factory auto floor */
        GR2_FN_DISPATCH_CMD("svautoset 100 25600 auto");
    }
}

/* Feature 4: Fine-Grain High-ISO Noise Reduction */
void hooks_set_fine_grain_nr(bool enable) {
    if (enable) {
        /* Preserves grain up to ISO 1600 */
        GR2_FN_DISPATCH_CMD("nrset wms 100 800 1600");
    } else {
        /* Restore factory NR thresholds */
        GR2_FN_DISPATCH_CMD("nrset wms 100 400 800");
    }
}

/* Slot Name Patching (Only applied when user assigns a film to a slot) */
void hooks_patch_slot_name(AssignableSlot slot, const char *film_name) {
    switch (slot) {
        case SLOT_POSI:
            str_to_utf16_be(GR2_ADDR_NAME_POSI, film_name ? film_name : "Positive Film", 14);
            break;
        case SLOT_BLEACH:
            str_to_utf16_be(GR2_ADDR_NAME_BLEACH, film_name ? film_name : "Bleach Bypass", 14);
            break;
        case SLOT_RETRO:
            str_to_utf16_be(GR2_ADDR_NAME_RETRO, film_name ? film_name : "Retro", 6);
            break;
        case SLOT_CROSS:
            str_to_utf16_be(GR2_ADDR_NAME_CROSS, film_name ? film_name : "Cross Process", 14);
            break;
        default:
            break;
    }
}
