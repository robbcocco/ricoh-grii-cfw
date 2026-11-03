#ifndef FXPROBE_H
#define FXPROBE_H
#include <stdint.h>
/* H1 halation probe, debug builds only (src/fxprobe.c, finding 37 §7): DEV "Halation probe" retargets both calls
 * of the still effect step (GR2_FX_SITE_A/B) to the stage-1 veneer 0xA001E6CE. Per shot the hook runs the stock
 * step, then records the frame context, times a full read of the Y plane (cached and uncached) and stamps a
 * red-tinted 512x512 square (Cr up, Y unchanged) at the frame centre; the CFW task logs the record. */
#if CFW_DEBUG
struct cfw_boot;
void fxprobe_init(const struct cfw_boot *boot);   /* cfw_hooks_install: the veneer and its target word */
int fxprobe_set(int on);       /* CFW task: arm (verify-first) / disarm (restore); 0 ok, -1 refused (ERROR logged) */
int fxprobe_armed(void);
uint32_t fxprobe_shots(void);  /* hook calls since the last arming */
void fxprobe_poll(void);       /* CFW task, every 20 ms: log a new shot record */
void fxprobe_hook(uint32_t ctx);   /* the veneer's target: replaces the stock step's call */
#endif
#endif /* FXPROBE_H */
