#ifndef SETTINGS_H
#define SETTINGS_H

/* M5 settings engine (src/settings.c). CFW task only, except settings_early/settings_labels (flashed loader,
 * boot task, before the CFW task touches settings) and settings_log_status (also from the shell). */
void settings_early(void);                /* loader, IQ-ready call: apply from the loaded config, no file writes */
void settings_labels(void);               /* the menus are installed: Snap labels may be patched */
void settings_init(void);                 /* CFW task: load + apply (unless settings_early ran), labels, save */
void settings_poll(void);                 /* ~200 ms: re-apply after mode/MY changes, TAv sync */
int settings_count(void);
int settings_at(int k);                   /* setting shown as row k (display order, grouped by function) */
int settings_is_toggle(int i);            /* two values: OK toggles it */
int settings_tab_count(void);
const char *settings_tab_name(int tab);   /* <= 5 characters */
int settings_tab(int i);                  /* tab of setting i */
const char *settings_label(int i);
const char *settings_value(int i);        /* text for the current value */
void settings_step(int i, int dir);       /* dir -1/+1: change, apply, mark config dirty */
void settings_save(void);                 /* write the config to D: if changed */
void settings_log_status(void);           /* debug builds only */

#endif /* SETTINGS_H */
