#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"

#define LINE_MAX 128
#define HOLD_MAX 2048                  /* per buffer: the boot task (+ other tasks) and the TTY task */
#define TASK_STACK 0x3000              /* src/task.c CFW_TASK_STACK */

void (*volatile cfw_log_tap)(const char *line, uint32_t len);
static uint8_t g_held;
/* While held, the boot task (hooks, lowest priority) and the early TTY task (higher) both log; the TTY task can
 * preempt a boot-task append but not the other way round, so each gets its own buffer (released boot first). */
static struct { char buf[HOLD_MAX]; uint32_t len, lost; } g_hold[2];
static uint32_t g_task_top;            /* an address near the top of the TTY task's stack, 0 before it runs */

void cfw_log_task_stack(uint32_t top) {
    g_task_top = top;
}

static void file_append(const char *buf, uint32_t len) {
    int fd = GR2_FN_FILE_OPEN(CFW_LOG_PATH, GR2_FILE_APPEND);
    if (fd == 0) return;
    GR2_FN_FILE_WRITE(fd, buf, len);
    GR2_FN_FILE_CLOSE(fd);
}

static void log_write(const char *buf, uint32_t len) {
    void (*tap)(const char *, uint32_t) = cfw_log_tap;
    if (tap) tap(buf, len);
    if (!g_held) {
        file_append(buf, len);
        return;
    }
    uint32_t sp = (uint32_t)&tap;
    int t = g_task_top && sp <= g_task_top && sp > g_task_top - TASK_STACK;
    if (g_hold[t].len + len <= HOLD_MAX) {
        for (uint32_t i = 0; i < len; i++) g_hold[t].buf[g_hold[t].len + i] = buf[i];
        g_hold[t].len += len;
    } else {
        g_hold[t].lost++;
    }
}

void cfw_log_hold(void) {
    g_held = 1;
}

void cfw_log_release(void) {
    if (!g_held) return;
    g_held = 0;
    uint32_t lost = 0;
    for (int t = 0; t < 2; t++) {
        if (g_hold[t].len) file_append(g_hold[t].buf, g_hold[t].len);
        lost += g_hold[t].lost;
        g_hold[t].len = g_hold[t].lost = 0;
    }
    if (lost) cfw_log_hex("ERROR log: lines lost while held", lost);
}

/* Copy at most `room` characters of s; returns the new end. */
static char *put_bounded(char *p, const char *s, uint32_t room) {
    while (*s && room--) *p++ = *s++;
    return p;
}

void cfw_log(const char *msg) {
    char buf[LINE_MAX];
    char *p = put_bounded(buf, msg, LINE_MAX - 1);
    *p++ = '\n';
    log_write(buf, (uint32_t)(p - buf));
}

void cfw_log_hex(const char *label, uint32_t value) {
    char buf[LINE_MAX];
    char *p = put_bounded(buf, label, LINE_MAX - 12);
    p = put_str(p, " 0x");
    p = put_hex32(p, value);
    *p++ = '\n';
    log_write(buf, (uint32_t)(p - buf));
}

void cfw_log_hex2(const char *label, uint32_t a, uint32_t b) {
    char buf[LINE_MAX];
    char *p = put_bounded(buf, label, LINE_MAX - 23);
    p = put_str(p, " 0x");
    p = put_hex32(p, a);
    p = put_str(p, " 0x");
    p = put_hex32(p, b);
    *p++ = '\n';
    log_write(buf, (uint32_t)(p - buf));
}
