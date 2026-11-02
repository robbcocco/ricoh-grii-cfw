/* Registers CFW shell commands in the RTOS shell registry so TTL scripts
 * (`sprintf "cfw status"` + `sendln inputstr`) and telnet can drive the CFW.
 * Shell output goes to the UART; results are reported through the log file
 * and the handler return value (telnet shows "result: %d"). */
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"
#include "settings.h"

/* Stock registry holds 97 base + 825 A:/command.txt records. */
#define SHELL_COUNT_MIN 900
#define SHELL_COUNT_MAX 1000

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

#if CFW_DEBUG
/* Session state into the debug log: `cfw status` and the power-off hook (src/hooks.c). */
void cfw_status_log(void) {
    cfw_log_hex("status: systime now", GR2_FN_SYSTEM_GETTIME());
    cfw_log_hex("status: task start systime", cfw_task_start_time());
    cfw_log_hex("status: last beat systime", cfw_task_last_beat_time());
    cfw_log_hex("status: task heartbeat (beats)", cfw_task_heartbeat());
    cfw_log_hex("status: shell commands", (uint32_t)*GR2_SHELL_CTX_COUNT);
    cfw_log_hex("status: prop EFX_BTN (0x1A6)", GR2_FN_PROP_GET(GR2_PROPID_EFX_BTN));
    cfw_log_hex("status: prop EFX_BTN_CONT (0x1A7)", GR2_FN_PROP_GET(GR2_PROPID_EFX_BTN_CONT));
    settings_log_status();
    cfw_input_dump_trace();
}
#endif

/* `cfw status`: debug builds log the session state; release builds only return the heartbeat. */
static int cmd_cfw(int argc, char **argv) {
    if (argc >= 2 && streq(argv[1], "status")) {
#if CFW_DEBUG
        cfw_status_log();
#endif
        return (int)cfw_task_heartbeat();
    }
    DBG("usage: cfw status");
    return -1;
}

static gr2_shell_cmd_t g_cmds[] = {
    { "cfw", cmd_cfw },
};

int cfw_shell_register(void) {
    int32_t count = *GR2_SHELL_CTX_COUNT;
    void *list = *GR2_SHELL_CTX_LIST;
    DBG_HEX("shell: commands before", (uint32_t)count);
    if (list == 0 || count < SHELL_COUNT_MIN || count > SHELL_COUNT_MAX) {
        ERR_HEX("shell: registry not as expected, not registering (count)", (uint32_t)count);
        return -1;
    }
    for (uint32_t i = 0; i < sizeof(g_cmds) / sizeof(g_cmds[0]); i++) {
        /* Append the node first, then publish the new count: a concurrent
         * lookup sees either the old count or a fully linked node. */
        *GR2_SHELL_CTX_LIST = GR2_FN_LIST_APPEND(*GR2_SHELL_CTX_LIST, &g_cmds[i]);
        *GR2_SHELL_CTX_COUNT = *GR2_SHELL_CTX_COUNT + 1;
    }
    DBG_HEX("shell: commands after", (uint32_t)*GR2_SHELL_CTX_COUNT);
    return 0;
}
