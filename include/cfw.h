#ifndef CFW_H
#define CFW_H
#include <stdint.h>

#ifndef CFW_VERSION
#define CFW_VERSION "dev"   /* Makefile passes the VERSION file */
#endif

/* Resident task (src/task.c) */
int cfw_task_start(void);              /* task id (TCB pointer, 0xA09xxxxx) from Task_Create, 0 on failure */
uint32_t cfw_task_heartbeat(void);     /* seconds alive (one beat per ~1 s) */
uint32_t cfw_task_start_time(void);    /* System_GetTime() when the task started */
uint32_t cfw_task_last_beat_time(void);/* System_GetTime() at the latest beat */
void cfw_task_poll(void);              /* one 20 ms iteration of the task loop (the task body; tests) */
/* Boot phases the task waits for (src/main.c); all 1 on the Startup.ttl path. */
int cfw_phase_card(void);              /* H: readable: the boot task's remount after section 9 is over */
int cfw_phase_late(void);              /* loader hooks over and JobSystem step 13 done: settings, looks and
                                          file writes belong to the task */

/* Shell commands (src/shell.c) */
int cfw_shell_register(void);          /* 0 on success */
void cfw_status_log(void);             /* debug: session state into the log (`tty status`, power-off) */

/* Menu choices (src/menu.c). Each Effect choice opens one TTY menu:
 * prop 0x1A6 value CFW_EFX_VALUE + menu, prop 0x1A7 value CFW_HOLD_VALUE + menu. */
#define CFW_MENU_SETTINGS 0                /* "TTY Menu": tabbed settings */
#define CFW_MENU_RECIPES  1                /* "Recipes": recipe carousel */
#define CFW_MENU_COUNT    2
#define CFW_EFX_VALUE  0x20                /* prop 0x1A6: first value past the 32 stock functions */
#define CFW_HOLD_VALUE 0x02                /* prop 0x1A7: after Wi-Fi (0) and Aperture Preview (1) */
int cfw_menu_install(void);            /* 0 on success */

/* Effect button hooks (src/hooks.c) */
struct cfw_boot;
int cfw_hooks_install(const struct cfw_boot *boot);  /* 0 on success */
int cfw_hooks_install_iq(const struct cfw_boot *boot, void (*fn)(void));   /* loader, hook(5); 0 on success */
void cfw_iq_ready_orig(void);          /* the stock call the IQ-ready hook replaced */
uint32_t cfw_hook_requests(int menu);  /* requests for CFW_MENU_* seen so far */

/* Overlay (src/ui.c), TTY task only */
void cfw_ui_request(int menu);         /* open `menu`, or close if any menu is open */
int cfw_ui_is_open(void);
void cfw_ui_key(uint32_t id, uint32_t state);  /* id = 0x42NN key message, state 0 = release */
void cfw_ui_set_recipes(const char *const *names, int count);  /* carousel entries (not copied) */
void cfw_ui_tick(void);                /* ~1 s: refresh live values (INFO/DEV tabs) */

/* Key capture (src/input.c) */
int cfw_input_install(void);           /* 0 on success */
void cfw_input_capture(int on);        /* consume key messages while on */
int cfw_input_pop(uint32_t *id, uint32_t *state);  /* 1 if an event was returned */
void cfw_input_dump_trace(void);       /* debug builds: log the last raw key messages */

#endif /* CFW_H */
