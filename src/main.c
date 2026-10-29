#include "gr2_hw.h"
#include "gr2_ui.h"

void cfw_log(const char *msg);
void hooks_init(void);

int cfw_main(void) {
    /* 1. Log entry to H:/cfw_log.txt directly from native C */
    cfw_log("--> CFW: cfw_main() entered successfully at 0xA0A00000!\n");

    /* 2. Initialize Custom OS state (All features & recipes start OFF / Stock by default) */
    ui_init();
    cfw_log("--> CFW: ui_init() completed (all features neutral/off)\n");

    /* 3. Register unconditional Effect Button Hold hook & patch menu text */
    hooks_init();
    cfw_log("--> CFW: hooks_init() completed (0xA03E9984 hooked to hook_efx_hold)\n");

    return 0;
}
