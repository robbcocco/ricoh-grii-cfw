#ifndef SETTINGS_H
#define SETTINGS_H

/* M5 settings engine (src/settings.c). CFW task only, except
 * settings_log_status (also called from the shell). */
void settings_init(void);                 /* load config, apply enabled settings */
void settings_poll(void);                 /* ~200 ms: re-apply after mode/MY changes, TAv sync */
int settings_count(void);
int settings_tab_count(void);
const char *settings_tab_name(int tab);   /* <= 5 characters */
int settings_tab(int i);                  /* tab of setting i */
const char *settings_label(int i);
const char *settings_value(int i);        /* text for the current value */
void settings_step(int i, int dir);       /* dir -1/+1: change, apply, mark config dirty */
void settings_save(void);                 /* write the config to D: if changed */
void settings_log_status(void);           /* debug builds only */

#endif /* SETTINGS_H */
