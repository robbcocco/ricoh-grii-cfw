/* Resident CFW task. With the flashed loader it is created at hook(10), right
 * after the menus and Effect hooks are installed (finding 35 §3), and runs in
 * phases while the boot continues:
 *   - until JobSystem step 10 (APP exists): nothing but waiting;
 *   - then every 20 ms: Effect-button requests, key input, INFO and the VIEW
 *     overlays, a 1 s heartbeat for `cfw status`;
 *   - fonts once the card is readable (after the boot task's remount); menu
 *     requests wait for them, the Recipes menu also for the recipe scan;
 *   - "late" (loader hooks over, step 13 done): settings_init (the settings
 *     were applied at boot already: it records labels and saves the config),
 *     the recipe scan and the 200 ms settings / recipe-look polls.
 * On the Startup.ttl path all phases are open at once (cfw_main starts it). */
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"
#include "info.h"
#include "osd.h"
#include "overlay.h"
#include "recipes.h"
#include "settings.h"

#define CFW_TASK_PRIORITY 0x96   /* same as telnetd / Script */
#define CFW_TASK_STACK    0x3000 /* telnetd uses 0x3000; src/log.c TASK_STACK must match */
#define CFW_POLL_MS       20     /* Task_Delay is in ms (verified on hardware) */
#define POLLS_PER_BEAT    (1000 / CFW_POLL_MS)
#define POLLS_PER_SETTING (200 / CFW_POLL_MS)

static volatile uint32_t g_heartbeat;
static volatile uint32_t g_start_time;
static volatile uint32_t g_last_beat_time;

static uint32_t g_seen[CFW_MENU_COUNT];
static int g_task_started, g_task_fonts, g_task_late;
static uint32_t g_polls, g_setting_polls;

void cfw_task_poll(void) {
    if (!g_task_started) {
        if (!GR2_BOOT_STEP_DONE(10) && !cfw_phase_late()) return;   /* APP and the OSD do not exist yet */
        g_task_started = 1;
        DBG_HEX2("task: loop live (task start, now) systime", g_start_time, GR2_FN_SYSTEM_GETTIME());
    }
    if (!g_task_late && cfw_phase_late()) {
        settings_init();
        recipes_init();
        recipes_poll();                 /* a slot's look before the fonts on the Startup.ttl path */
        g_task_late = 1;
        DBG_HEX("task: late phase, systime", GR2_FN_SYSTEM_GETTIME());
    }
    if (!g_task_fonts && cfw_phase_card()) {
        osd_fonts_load();
        g_task_fonts = 1;
    }
    for (int m = 0; m < CFW_MENU_COUNT; m++) {
        /* A press before its menu can open stays pending (the counter is not consumed). */
        if (!g_task_fonts || (m == CFW_MENU_RECIPES && !g_task_late)) continue;
        uint32_t req = cfw_hook_requests(m);
        if (req != g_seen[m]) {
            g_seen[m] = req;
            cfw_ui_request(m);
        }
    }
    uint32_t id, state;
    while (cfw_input_pop(&id, &state)) cfw_ui_key(id, state);
    info_poll();
    overlay_poll();
    if (g_task_late && ++g_setting_polls >= POLLS_PER_SETTING) {
        g_setting_polls = 0;
        settings_poll();
        recipes_poll();
    }
    if (++g_polls == POLLS_PER_BEAT) {
        g_polls = 0;
        g_last_beat_time = GR2_FN_SYSTEM_GETTIME();
        g_heartbeat++;
        cfw_ui_tick();
    }
}

static void cfw_task(uint32_t arg) {
    cfw_log_task_stack((uint32_t)&arg);
    g_start_time = GR2_FN_SYSTEM_GETTIME();
    for (int m = 0; m < CFW_MENU_COUNT; m++) g_seen[m] = cfw_hook_requests(m);
    DBG_HEX("task: running, systime", g_start_time);
    for (;;) {
        cfw_task_poll();
        GR2_FN_DELAY(CFW_POLL_MS);
    }
}

int cfw_task_start(void) {
    return GR2_FN_TASK_CREATE(cfw_task, 0, CFW_TASK_PRIORITY, CFW_TASK_STACK, "CFW");
}

uint32_t cfw_task_heartbeat(void) {
    return g_heartbeat;
}

uint32_t cfw_task_start_time(void) {
    return g_start_time;
}

uint32_t cfw_task_last_beat_time(void) {
    return g_last_beat_time;
}
