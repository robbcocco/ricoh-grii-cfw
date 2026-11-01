/* Stage 2 entry, called once per power cycle by stage 1 (src/boot.c) after
 * relocation: register the `cfw` shell command, start the resident CFW task,
 * add the Effect-button menu choices and hooks, then return to the shell. */
#include "boot.h"
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"
#include "info.h"

int cfw_main(const cfw_boot_t *boot) {
    DBG("=== gr2_cfw " CFW_VERSION " debug ===");
    DBG_HEX("main: loaded at", boot->base);
    if (boot->loader)
        DBG_HEX2("main: started by the flashed loader (stage 2 read at, now) systime", boot->t_preload,
                 GR2_FN_SYSTEM_GETTIME());
    info_set_main_base(boot->base);
    if (cfw_shell_register() != 0) return -1;
    /* Menus first: they replace the language tables that the task's settings
     * (Snap labels) patch. */
    if (cfw_menu_install() == 0 && cfw_hooks_install(boot) == 0) cfw_input_install();
    int task = cfw_task_start();
    if (task == 0) {   /* the id is a TCB pointer (0xA09xxxxx); stock parks the caller on failure (finding 26) */
        ERR_HEX("Task_Create failed", (uint32_t)task);
        return -1;
    }
    DBG_HEX("task id", (uint32_t)task);
    DBG("init done");
    return 0;
}
