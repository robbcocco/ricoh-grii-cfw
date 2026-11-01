/* Resident CFW task: everything that must outlive `ex` runs here.
 * Loads and applies the settings and the active recipe look first (speed at
 * power-on), then the fonts; polls the Effect-button hook counter every 20 ms
 * and toggles the overlay, runs the settings poll every 200 ms and keeps a 1 s
 * heartbeat for `cfw status` (and the INFO/DEV tab refresh). */
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"
#include "info.h"
#include "osd.h"
#include "overlay.h"
#include "recipes.h"
#include "settings.h"

#define CFW_TASK_PRIORITY 0x96   /* same as telnetd / Script */
#define CFW_TASK_STACK    0x3000 /* telnetd uses 0x3000 */
#define CFW_POLL_MS       20     /* Task_Delay is in ms (verified on hardware) */
#define POLLS_PER_BEAT    (1000 / CFW_POLL_MS)
#define POLLS_PER_SETTING (200 / CFW_POLL_MS)

static volatile uint32_t g_heartbeat;
static volatile uint32_t g_start_time;
static volatile uint32_t g_last_beat_time;

static void cfw_task(uint32_t arg) {
    (void)arg;
    uint32_t seen[CFW_MENU_COUNT];
    for (int m = 0; m < CFW_MENU_COUNT; m++) seen[m] = cfw_hook_requests(m);
    uint32_t polls = 0, setting_polls = 0;
    g_start_time = GR2_FN_SYSTEM_GETTIME();
    DBG_HEX("task: running, systime", g_start_time);
    settings_init();
    recipes_init();
    recipes_poll();                     /* a recipe slot's look before anything slower */
    osd_fonts_load();
    for (;;) {
        GR2_FN_DELAY(CFW_POLL_MS);
        for (int m = 0; m < CFW_MENU_COUNT; m++) {
            uint32_t req = cfw_hook_requests(m);
            if (req != seen[m]) {
                seen[m] = req;
                cfw_ui_request(m);
            }
        }
        uint32_t id, state;
        while (cfw_input_pop(&id, &state)) cfw_ui_key(id, state);
        info_poll();
        overlay_poll();
        if (++setting_polls == POLLS_PER_SETTING) {
            setting_polls = 0;
            settings_poll();
            recipes_poll();
        }
        if (++polls == POLLS_PER_BEAT) {
            polls = 0;
            g_last_beat_time = GR2_FN_SYSTEM_GETTIME();
            g_heartbeat++;
            cfw_ui_tick();
        }
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
