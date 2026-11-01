#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"

#define LINE_MAX 128

void (*volatile cfw_log_tap)(const char *line, uint32_t len);
volatile uint8_t cfw_log_mute;

static void log_write(const char *buf, uint32_t len) {
    void (*tap)(const char *, uint32_t) = cfw_log_tap;
    if (tap) tap(buf, len);
    if (cfw_log_mute) return;
    int fd = GR2_FN_FILE_OPEN(CFW_LOG_PATH, GR2_FILE_APPEND);
    if (fd == 0) return;
    GR2_FN_FILE_WRITE(fd, buf, len);
    GR2_FN_FILE_CLOSE(fd);
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
