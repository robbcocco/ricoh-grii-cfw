#ifndef CFW_LOG_H
#define CFW_LOG_H
#include <stdint.h>

#define CFW_LOG_PATH "H:/cfw/cfw_log.txt"

/* Build variants (Makefile): CFW_DEBUG=1 writes every log line, CFW_DEBUG=0
 * (release, rom) writes only ERR lines; DBG calls compile to nothing and their
 * arguments are not evaluated. Messages passed to ERR must be literals.
 * CFW_ROM=1 (rom): the CFW lives in the release firmware's A: drive (the project plan):
 * code, fonts and built-in recipes from A:/cfw, no card access in the boot hooks. */
#ifndef CFW_DEBUG
#define CFW_DEBUG 1
#endif
#ifndef CFW_ROM
#define CFW_ROM 0
#endif

/* Append one line to CFW_LOG_PATH: "msg", "label 0xV", "label 0xA 0xB". */
void cfw_log(const char *msg);
void cfw_log_hex(const char *label, uint32_t value);
void cfw_log_hex2(const char *label, uint32_t a, uint32_t b);
/* Optional copy of every log line (with its '\n') as written; set by the
 * developer console (src/console.c). Must not block or log. */
extern void (*volatile cfw_log_tap)(const char *line, uint32_t len);
/* Hold: lines go to RAM instead of the card (the loader's section-load hooks,
 * which must not write H: before the boot task's remount after section 9,
 * finding 32). Each stage (stage 1, stage 2) has its own buffers: 2 KiB for
 * the boot task (and other tasks), 2 KiB for the early CFW task. */
void cfw_log_hold(void);
/* Write the held lines (boot task's, then the CFW task's; plus a count of
 * lines lost to a full buffer) and log to the card again. No-op if not holding. */
void cfw_log_release(void);
/* The CFW task tells the log where its stack is (an address near its top),
 * so held lines from it go to its own buffer. */
void cfw_log_task_stack(uint32_t top);

#define ERR(msg)              cfw_log("ERROR " msg)
#define ERR_HEX(label, v)     cfw_log_hex("ERROR " label, (v))
#define ERR_HEX2(label, a, b) cfw_log_hex2("ERROR " label, (a), (b))

#if CFW_DEBUG
#define DBG(msg)              cfw_log(msg)
#define DBG_HEX(label, v)     cfw_log_hex(label, (v))
#define DBG_HEX2(label, a, b) cfw_log_hex2(label, (a), (b))
#else
#define DBG(msg)              ((void)0)
#define DBG_HEX(label, v)     ((void)sizeof(v))
#define DBG_HEX2(label, a, b) ((void)sizeof(a), (void)sizeof(b))
#endif

#endif /* CFW_LOG_H */
