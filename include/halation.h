#ifndef HALATION_H
#define HALATION_H
#include <stdint.h>
#include "look.h"

/* Film halation in final stills (src/halation.c, finding 37 §4.1): a red-dominant glow around highlights, added
 * to the developed frame in the still effect step (before the review image and the JPEG) while the shown recipe
 * look has `halation`. Never in live view, in RAW development or in a DNG's embedded images. */
struct cfw_boot;
void halation_init(const struct cfw_boot *boot);   /* cfw_hooks_install: the effect veneer and its target word */
/* CFW task, every recipe-look poll: l = the look shown (0: stock), cm = the colour mode it is shown in (-1: none),
 * gen = changes whenever src/look.c wrote IQ records. Rebuilds the pass tables when the look, the mode's contrast,
 * saturation or colour space changed; retargets the effect-step call (verify-first) only while l has halation and
 * restores it otherwise. */
void halation_sync(const look_t *l, int cm, uint32_t gen);
int halation_installed(void);                       /* the effect-step call goes through the CFW */
void halation_poll(void);                           /* CFW task, 20 ms: report the last shot (debug log; ERROR) */
void halation_hook(uint32_t ctx);                   /* the effect veneer's target */

#endif /* HALATION_H */
