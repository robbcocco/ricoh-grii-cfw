#ifndef CFW_LOG_H
#define CFW_LOG_H
#include <stdint.h>

#define CFW_LOG_PATH "H:/cfw/cfw_log.txt"

/* Build variants (Makefile): CFW_DEBUG=1 writes every log line, CFW_DEBUG=0
 * (release) writes only ERR lines; DBG calls compile to nothing and their
 * arguments are not evaluated. Messages passed to ERR must be literals. */
#ifndef CFW_DEBUG
#define CFW_DEBUG 1
#endif

/* Append one line to CFW_LOG_PATH: "msg", "label 0xV", "label 0xA 0xB". */
void cfw_log(const char *msg);
void cfw_log_hex(const char *label, uint32_t value);
void cfw_log_hex2(const char *label, uint32_t a, uint32_t b);
/* Optional copy of every log line (with its '\n') as written; set by the
 * developer console (src/console.c). Must not block or log. */
extern void (*volatile cfw_log_tap)(const char *line, uint32_t len);
/* Non-zero: lines go only to the tap, not to the card (stage 1 at the loader's
 * hook(1), when the card may belong to a PC in a USB boot). */
extern volatile uint8_t cfw_log_mute;

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
