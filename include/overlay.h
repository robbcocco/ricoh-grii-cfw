#ifndef OVERLAY_H
#define OVERLAY_H
#include <stdint.h>

/* Persistent graphics over live view in the camera's RGBA4444 "full" plane
 * (display layer 2), next to the stock shooting UI (src/overlay.c, finding 23,
 * Tier A: TTY stamps into the buffer stock currently shows). TTY task only. */
enum { FRAME_OFF, FRAME_XPAN, FRAME_6X7, FRAME_4X5, FRAME_1X1, FRAME_16X9, FRAME_COUNT };

void overlay_set_frame(int frame);        /* FRAME_* */
void overlay_set_zebra(int level);        /* 0 off, 1..3 luma thresholds (ZEBRA_Y in overlay.c) */
void overlay_poll(void);                  /* TTY task, every 20 ms */

/* DEV tab hardware checks (finding 23 §4.5): log the plane/screen state; toggle a
 * test pattern (opaque red, 50 % red, diagonal) in the shown buffer. */
void overlay_log_state(void);
int overlay_test_pattern(void);           /* 1 drawn, 0 removed, -1 full plane not on screen */

#endif /* OVERLAY_H */
