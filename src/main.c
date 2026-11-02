/* Stage 2 entries (stage 1, src/boot.c):
 *   cfw_hook(n)  flashed loader only: after every APP section load n, in the boot task, before
 *                section n has run (M10c, findings 32, 35). First hook (stub v2: section 3): read the
 *                config and the MY slots' looks; hook(5): hook the IQ-ready call in section 5, where
 *                the CFW settings and the slot's look are applied after IQ.bin, AE_F, AF_F are loaded
 *                and before live view starts; hook(10): install the menus, Effect hooks and input
 *                capture into section 10 before it first runs, then start the CFW task; every hook:
 *                settings and look if the IQ-ready call missed them. Log lines are held in RAM (the
 *                card is remounted after section 9) and written by cfw_main. The rom build (CFW_ROM, release
 *                firmware) reads only D: and A: in the hooks, never the card.
 *   cfw_main     once per power cycle: after JobSystem step 13 (loader) or from Startup.ttl:
 *                register the `cfw` shell command, install whatever the hooks did not, start the
 *                resident CFW task unless the hooks did. */
#include "boot.h"
#include "cfw.h"
#include "cfw_log.h"
#include "config.h"
#include "gr2_hw.h"
#include "info.h"
#include "recipes.h"
#include "settings.h"

#define PATCH_SECTION 10               /* menu records, string tables, Effect BLs, input slot */
#define IQ_SECTION    5                /* IQ-ready BL; CapInit(5) loads IQ.bin, AF_F, AE_F (findings 32, 35) */
#define MOUNT_SECTION 9                /* the boot task remounts H: right after this hook (finding 31) */

static int g_installed = 1;            /* 1: not tried yet; else the result of install() */
static int g_early;                    /* the loader's hooks ran */
static int g_look_done, g_set_done;
static int g_cfg_early;                /* settings_early may run: a config was loaded at the first hook (rom) */
static int g_iq_seen, g_seen5, g_seen9;
static volatile int g_card_ready, g_main_done;
static int g_task;                     /* CFW task id once created */

int cfw_phase_card(void) {
    return !g_early || g_card_ready;
}

/* cfw_main (CFWldr, after JobSystem step 13) has written the held log and the timeline: from then on the task owns
 * settings, looks and file writes, and nothing runs CFW code in parallel any more. */
int cfw_phase_late(void) {
    return !g_early || g_main_done;
}

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

/* The settings need the config (first hook), final mode/MY props and the tables CapInit(5) loads: the IQ-ready
 * call, or any hook after hook(5) if that call was missed. */
static void settings_try(uint32_t n) {
    if (g_set_done || GR2_MODE_PROPS_FINAL != 1 || !(g_iq_seen || (g_seen5 && n != IQ_SECTION))) return;
    g_set_done = 1;
    if (g_cfg_early) settings_early();
}

/* CapInit(5), boot task: IQ.bin, AF_F and AE_F are in RAM, live view has not started (notify(5) follows). */
static void iq_ready(void) {
#if CFW_DEBUG
    trace(0);
#endif
    g_iq_seen = 1;
    settings_try(0);
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
        /* Reads only, files closed before the hook returns. debug/release: D:, else the card copy (the stub mounted
         * the card). rom: D: only, no card access in the boot hooks; without a config there, settings_init applies
         * the card copy in the task's late phase and the boot runs with stock settings until then. */
        g_cfg_early = cfg_load(!CFW_ROM) || !CFW_ROM;
        recipes_early_load();
    }
#if CFW_DEBUG
    trace(n);
#endif
    if (g_seen9) g_card_ready = 1;     /* the first hook after hook(9): the remount is over */
    if (n == MOUNT_SECTION) g_seen9 = 1;
    if (n == IQ_SECTION) {
        g_seen5 = 1;
        if (!g_look_done || !g_set_done) (void)cfw_hooks_install_iq(boot, iq_ready);
    }
    settings_try(n);
    if (n == PATCH_SECTION && g_installed == 1) {
        g_installed = install(boot);
        DBG_HEX("main: installed at hook(10), rc", (uint32_t)g_installed);
        if (g_installed == 0) {
            settings_labels();         /* the menus' language tables exist now */
            g_task = cfw_task_start();
            if (!g_task) ERR("main: Task_Create failed at hook(10), starting at step 13");
        }
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
    if (!g_task) {
        g_task = cfw_task_start();
        if (g_task == 0) {   /* the id is a TCB pointer (0xA09xxxxx); stock parks the caller on failure (finding 26) */
            ERR("Task_Create failed");
            return -1;
        }
    }
    DBG_HEX("task id", (uint32_t)g_task);
    DBG("init done");
    g_main_done = 1;
    return 0;
}
