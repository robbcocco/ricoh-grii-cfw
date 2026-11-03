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
 * function.
 *
 * Debug builds also retarget the power-off job's call that runs H:/Shutdown.ttl
 * (finding 26 §4, JOBID_POWER_OFF 0xA00E98B8): the session state is logged
 * there, in the FRM task, before the stock call runs a Shutdown.ttl if one
 * exists. Nothing on the card is needed for it.
 *
 * The still effect-step veneer goes to src/halation.c, which patches its BL
 * only while the shown recipe look has halation. */
#include <stdint.h>
#include "boot.h"
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"
#include "halation.h"

uint32_t cfw_cpsr(void);           /* src/arm.S */

#define SITE_PRESS      0xA03A1F92u
#define ORIG_PRESS      0xA039607Cu
#define SITE_HOLD       0xA03A5790u
#define ORIG_HOLD       0xA0396EA0u
#define SITE_IQ         0xA01B584Au   /* section 5, FUN_a01b5828: once, right after IQ.bin is loaded */
#define ORIG_IQ         0xA01C2A80u   /* void FUN_a01c2a80(void): creates "SceneIFMsgQ" (finding 32) */
#define SITE_OFF        0xA00E9950u   /* JOBID_POWER_OFF: bl FUN_a00ea1d4("*:/Shutdown.ttl", the card's) */
#define ORIG_OFF        0xA00EA1D4u   /* int (const char *path): 0 = script started, the job waits */

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

/* The Thumb BL at `site` calls `target`: 1, else 0 (also when it cannot reach it). */
int cfw_bl_calls(uint32_t site, uint32_t target) {
    const volatile uint16_t *p = (const volatile uint16_t *)site;
    uint32_t hi, lo;
    return encode_bl(site, target, &hi, &lo) == 0 && p[0] == hi && p[1] == lo;
}

/* Verify-first retarget of the BL at `site` from `from` to `to` (same low 12 bits): one halfword store. */
int cfw_bl_retarget(uint32_t site, uint32_t from, uint32_t to) {
    volatile uint16_t *p = (volatile uint16_t *)site;
    uint32_t ohi, olo, nhi, nlo;
    if (encode_bl(site, from, &ohi, &olo) != 0 || encode_bl(site, to, &nhi, &nlo) != 0 ||
        nlo != olo) {
        ERR_HEX2("hooks: veneer not reachable with one halfword (site, veneer)", site, to);
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

#if CFW_DEBUG
static int cfw_off_hook(const char *path) {
    DBG("status: power-off");
    cfw_status_log();
    return ((int (*)(const char *))(ORIG_OFF | 1))(path);
}
#endif

int cfw_hooks_install(const cfw_boot_t *boot) {
    DBG_HEX("hooks: cpsr", cfw_cpsr());
    /* The veneers (stage 1) jump through these words: fill them before any BL points there. */
    *boot->press_target = (uint32_t)cfw_press_hook;
    *boot->hold_target = (uint32_t)cfw_hold_hook;
    int rc = cfw_bl_retarget(SITE_PRESS, ORIG_PRESS, boot->veneer_press);
    rc |= cfw_bl_retarget(SITE_HOLD, ORIG_HOLD, boot->veneer_hold);
#if CFW_DEBUG
    *boot->off_target = (uint32_t)cfw_off_hook;
    (void)cfw_bl_retarget(SITE_OFF, ORIG_OFF, boot->veneer_off);   /* diagnostics only: never fails the install */
#endif
    halation_init(boot);
    return rc;
}

/* Loader only, at hook(5) (section 5 loaded, not yet run): CapInit(5) then calls fn
 * right after IQ.bin is in RAM and before notify(5) lets live view start; fn must
 * call cfw_iq_ready_orig() itself. */
int cfw_hooks_install_iq(const cfw_boot_t *boot, void (*fn)(void)) {
    *boot->iq_target = (uint32_t)fn;
    return cfw_bl_retarget(SITE_IQ, ORIG_IQ, boot->veneer_iq);
}

void cfw_iq_ready_orig(void) {
    ((void (*)(void))(ORIG_IQ | 1))();
}
