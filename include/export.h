#ifndef EXPORT_H
#define EXPORT_H
#include <stdint.h>

/* Export the camera's saved presets as recipe files in H:/tty/export (src/export.c, README
 * "Exporting your presets"). The export itself runs in the TTY task only: the DEV row calls
 * export_run, other tasks (the developer console) ask for it with export_request. */
int export_run(void);                     /* TTY task: write the files; files written, or -1 */
const char *export_result(void);          /* the last export's result line, "" before the first */
uint32_t export_request(void);            /* any task: ask the TTY task for an export; returns a ticket */
int export_served(uint32_t ticket);       /* that request has run (export_result holds its result) */
void export_poll(void);                   /* TTY task, late phase: run a requested export */

#endif /* EXPORT_H */
