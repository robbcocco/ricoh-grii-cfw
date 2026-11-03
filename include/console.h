#ifndef CONSOLE_H
#define CONSOLE_H
#include <stdint.h>

/* Developer console (src/console.c): a TTY-owned telnet server on port 23 (the
 * stock telnetd is not started). The stock shell prints to the UART, so stock
 * telnet only answered "result: N"; while the console runs, all printf text and
 * TTY log lines are captured in a ring buffer, and any stock shell command run
 * through it returns its output. No password, like stock telnetd: switched on
 * and off from the DEV tab; Wi-Fi itself is turned on in the camera menu. */
#define CONSOLE_PORT 23

enum { CONSOLE_OFF, CONSOLE_WAITING, CONSOLE_LISTENING };

int console_set(int on);                  /* 0 on success; capture + server on / off */
int console_state(void);                  /* CONSOLE_*: WAITING = on, no network socket yet */
/* Run one command line, writing at most max-1 bytes of output (NUL-terminated);
 * returns the output length, or -1 for `exit`. Used by the server per line and
 * by the emulator tests. */
int console_exec(const char *line, char *out, int max);

#endif /* CONSOLE_H */
