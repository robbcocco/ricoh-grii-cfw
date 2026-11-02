/* Stage 2 entries (stage 1, src/boot.c):
 *   cfw_hook(n)  flashed loader only: after every APP section load n, in the boot task, before
 *                section n has run (M10c, finding 32). First hook (stub v2: section 3): read the config and the MY
 *                slots' recipes; hook(5): hook the IQ-ready call in section 5, which shows the
 *                slot's look right after IQ.bin is loaded and before live view starts; hook(10):
 *                install the menus, Effect hooks and input capture into section 10 before it
 *                first runs; every hook: the slot's look if the IQ-ready call missed it. Log
 *                lines are held in RAM (the card is remounted after section 9) and written by
 *                cfw_main.
 *   cfw_main     once per power cycle: after JobSystem step 13 (loader) or from Startup.ttl:
 *                register the `cfw` shell command, install whatever the hooks did not, start
 *                the resident CFW task. */
#include "boot.h"
#include "cfw.h"
#include "cfw_log.h"
#include "config.h"
#include "gr2_hw.h"
#include "info.h"
#include "recipes.h"

#define PATCH_SECTION 10               /* menu records, string tables, Effect BLs, input slot */
#define IQ_SECTION    5                /* IQ-ready BL (finding 32 section 3.2) */

static int g_installed = 1;            /* 1: not tried yet; else the result of install() */
static int g_early;                    /* the loader's hooks ran */
static int g_look_done;

static int install(const cfw_boot_t *boot) {
    /* Menus first: they replace the language tables that the task's settings
     * (Snap labels) patch. */
    if (cfw_menu_install() != 0) return -1;
    if (cfw_hooks_install(boot) != 0) return -1;
    return cfw_input_install();
}

#if CFW_DEBUG
/* Boot timeline per hook (n = 0: the IQ-ready call), logged by cfw_main (findings 32, 33). */
#define TRACE_MAX 16
static struct { uint8_t n, props, step2, step5, lens; uint32_t t, lv; } g_trace[TRACE_MAX];
static int g_ntrace;

static void trace(uint32_t n) {
    if (g_ntrace == TRACE_MAX) return;
    g_trace[g_ntrace].n = (uint8_t)n;
    g_trace[g_ntrace].t = GR2_FN_SYSTEM_GETTIME();
    g_trace[g_ntrace].props = (uint8_t)GR2_MODE_PROPS_FINAL;
    g_trace[g_ntrace].step2 = (uint8_t)GR2_BOOT_STEP_DONE(2);
    g_trace[g_ntrace].step5 = (uint8_t)GR2_BOOT_STEP_DONE(5);
    g_trace[g_ntrace].lv = GR2_LV_DISP_ADDR;
    g_trace[g_ntrace].lens = GR2_LENS_MOTION_ID;
    g_ntrace++;
}

static void trace_log(void) {
    for (int i = 0; i < g_ntrace; i++) {
        cfw_log_hex2("hook: (n, systime)", g_trace[i].n, g_trace[i].t);
        cfw_log_hex2("hook:   (props final | step2 << 8 | step5 << 16 | lens script << 24, LV frame)",
                     g_trace[i].props | (uint32_t)g_trace[i].step2 << 8 | (uint32_t)g_trace[i].step5 << 16 |
                     (uint32_t)g_trace[i].lens << 24, g_trace[i].lv);
    }
}
#endif

/* CapInit(5), boot task: IQ.bin is in RAM, live view has not started (notify(5) follows). */
static void iq_ready(void) {
#if CFW_DEBUG
    trace(0);
#endif
    if (!g_look_done) g_look_done = recipes_early_look();
    cfw_iq_ready_orig();
}

void cfw_hook(uint32_t n, const cfw_boot_t *boot) {
    if (!g_early) {
        g_early = 1;
        cfw_log_hold();
        DBG("=== gr2_cfw " CFW_VERSION " debug ===");
        DBG_HEX("main: loaded at", boot->base);
        info_set_main_base(boot->base);
        cfg_load();                    /* card reads only; files closed before the hook returns */
        recipes_early_load();
    }
#if CFW_DEBUG
    trace(n);
#endif
    if (n == IQ_SECTION && !g_look_done) (void)cfw_hooks_install_iq(boot, iq_ready);
    if (n == PATCH_SECTION && g_installed == 1) {
        g_installed = install(boot);
        DBG_HEX("main: installed at hook(10), rc", (uint32_t)g_installed);
    }
    if (!g_look_done) g_look_done = recipes_early_look();
}

int cfw_main(const cfw_boot_t *boot) {
    if (g_early) {
        cfw_log_release();
    } else {
        DBG("=== gr2_cfw " CFW_VERSION " debug ===");
        DBG_HEX("main: loaded at", boot->base);
        info_set_main_base(boot->base);
    }
    if (boot->loader)
        DBG_HEX2("main: started by the flashed loader (stage 2 read at, now) systime", boot->t_preload,
                 GR2_FN_SYSTEM_GETTIME());
#if CFW_DEBUG
    trace_log();
#endif
    /* The shell command only serves `cfw status` (and logs its own refusal): menus and
     * hooks may already be live, so the task must start even without it. */
    (void)cfw_shell_register();
    if (g_installed == 1) g_installed = install(boot);
    int task = cfw_task_start();
    if (task == 0) {   /* the id is a TCB pointer (0xA09xxxxx); stock parks the caller on failure (finding 26) */
        ERR_HEX("Task_Create failed", (uint32_t)task);
        return -1;
    }
    DBG_HEX("task id", (uint32_t)task);
    DBG("init done");
    return 0;
}
