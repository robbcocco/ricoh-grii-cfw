/* Route the Effect button to the CFW when "CFW Menu" or "Recipes" is selected.
 * Two stock Thumb BLs are retargeted in RAM (finding 16 §4):
 *   press  0xA03A1F92  bl 0xA039607C (availability(value), r0 = assigned value)
 *   hold   0xA03A5790  bl 0xA0396EA0 (Wi-Fi toggle, no arguments)
 *
 * Tasks run in USR mode, so the I-cache cannot be invalidated. The patch is
 * therefore a single aligned 16-bit store of the BL's first halfword (the
 * veneers in src/veneers.S share the low 12 address bits of the stock
 * targets, so the second halfword is unchanged). A stale I-cache line can only
 * hold the complete old BL: until the line is refilled the stock function
 * runs, which is harmless. DRAM is write-through with no write buffer, so the
 * store reaches memory immediately.
 *
 * The hooks run in the camera's UI task: they only count requests per menu;
 * the CFW task does the work. Any other value falls through to the stock
 * function. */
#include <stdint.h>
#include "boot.h"
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"

uint32_t cfw_cpsr(void);           /* src/arm.S */

#define SITE_PRESS      0xA03A1F92u
#define ORIG_PRESS      0xA039607Cu
#define SITE_HOLD       0xA03A5790u
#define ORIG_HOLD       0xA0396EA0u

static volatile uint32_t g_requests[CFW_MENU_COUNT];

int cfw_press_hook(uint32_t value) {
    if (value >= CFW_EFX_VALUE && value < CFW_EFX_VALUE + CFW_MENU_COUNT) {
        g_requests[value - CFW_EFX_VALUE]++;
        return 0;   /* stock "no function" exit */
    }
    return ((int (*)(uint32_t))(ORIG_PRESS | 1))(value);
}

void cfw_hold_hook(void) {
    uint32_t value = GR2_FN_PROP_GET(GR2_PROPID_EFX_BTN_CONT);
    if (value >= CFW_HOLD_VALUE && value < CFW_HOLD_VALUE + CFW_MENU_COUNT) {
        g_requests[value - CFW_HOLD_VALUE]++;
        return;     /* skip the stock Wi-Fi toggle */
    }
    ((void (*)(void))(ORIG_HOLD | 1))();
}

uint32_t cfw_hook_requests(int menu) {
    return g_requests[menu];
}

/* Thumb BL encoding (ARMv5T: two 16-bit halves, +-4 MiB). */
static int encode_bl(uint32_t site, uint32_t target, uint32_t *hi, uint32_t *lo) {
    int32_t off = (int32_t)((target & ~1u) - (site + 4));
    if (off < -(1 << 22) || off >= (1 << 22)) return -1;
    *hi = 0xF000u | (((uint32_t)off >> 12) & 0x7FFu);
    *lo = 0xF800u | (((uint32_t)off >> 1) & 0x7FFu);
    return 0;
}

static int patch_site(uint32_t site, uint32_t orig, uint32_t veneer) {
    volatile uint16_t *p = (volatile uint16_t *)site;
    uint32_t ohi, olo, nhi, nlo;
    if (encode_bl(site, orig, &ohi, &olo) != 0 || encode_bl(site, veneer, &nhi, &nlo) != 0 ||
        nlo != olo) {
        ERR_HEX2("hooks: veneer not reachable with one halfword (site, veneer)", site, veneer);
        return -1;
    }
    if (p[0] != ohi || p[1] != olo) {
        ERR_HEX2("hooks: site not stock (site, found)", site, ((uint32_t)p[0] << 16) | p[1]);
        return -1;
    }
    p[0] = (uint16_t)nhi;
    DBG_HEX2("hooks: patched (site, now)", site, ((uint32_t)p[0] << 16) | p[1]);
    return 0;
}

int cfw_hooks_install(const cfw_boot_t *boot) {
    DBG_HEX("hooks: cpsr", cfw_cpsr());
    /* The veneers (stage 1) jump through these words: fill them before any BL points there. */
    *boot->press_target = (uint32_t)cfw_press_hook;
    *boot->hold_target = (uint32_t)cfw_hold_hook;
    int rc = patch_site(SITE_PRESS, ORIG_PRESS, boot->veneer_press);
    rc |= patch_site(SITE_HOLD, ORIG_HOLD, boot->veneer_hold);
    return rc;
}
