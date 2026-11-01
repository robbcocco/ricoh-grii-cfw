#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"

#define LINE_MAX 128
#define HOLD_MAX 2048

void (*volatile cfw_log_tap)(const char *line, uint32_t len);
static uint8_t g_held;
static char g_hold[HOLD_MAX];
static uint32_t g_hold_len, g_hold_lost;

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
    } else if (g_hold_len + len <= HOLD_MAX) {
        for (uint32_t i = 0; i < len; i++) g_hold[g_hold_len + i] = buf[i];
        g_hold_len += len;
    } else {
        g_hold_lost++;
    }
}

void cfw_log_hold(void) {
    g_held = 1;
}

void cfw_log_release(void) {
    if (!g_held) return;
    g_held = 0;
    if (g_hold_len) file_append(g_hold, g_hold_len);
    if (g_hold_lost) cfw_log_hex("ERROR log: lines lost while held", g_hold_lost);
    g_hold_len = g_hold_lost = 0;
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
