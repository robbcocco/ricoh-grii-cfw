#ifndef INFO_H
#define INFO_H
#include <stdint.h>
/* Read-only INFO tab and DEV (developer) tab of the CFW Menu (src/info.c,
 * finding 21). CFW task only. */
enum { INFO_PAGE_INFO, INFO_PAGE_DEV, INFO_PAGE_COUNT };

const char *info_page_name(int page);
int info_rows(int page);
const char *info_label(int page, int row);
const char *info_value(int page, int row);    /* valid until the next call */
int info_is_action(int page, int row);
/* Run an action row. Returns INFO_CLOSE when the menu must close first (the
 * action finishes later in info_poll). */
#define INFO_CLOSE 1
int info_action(int page, int row);
void info_poll(void);                         /* CFW task, every 20 ms: pending actions */
void info_set_main_base(uint32_t base);       /* shown on the DEV tab */
/* Wi-Fi state as the system controller sees it: system mode, UI mode, connect
 * mode, prop 0x1E8, WIFI_STATUS, Ctrl+0x214, +0x1B8, +0x1BC (also `wifi` in the
 * developer console). */
#define INFO_WIFI_N 8
void info_wifi_state(uint32_t v[INFO_WIFI_N]);

#endif /* INFO_H */
