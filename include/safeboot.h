#ifndef SAFEBOOT_H
#define SAFEBOOT_H
#include <stdint.h>
#include "gr2_hw.h"

/* M12 safe boot (the project plan, finding 30 §1.5): a 2-bit state S in RTC OPSR bits 2-3 (kept across power-off and
 * battery pulls, no flash wear; R2a [HW]).
 *   ARMED      the CFW runs; the first hook sets ONE_FAIL until the session proves healthy
 *   ONE_FAIL   the last session did not reach the healthy mark: the CFW runs once more, set DISARMED
 *   DISARMED   two unhealthy sessions in a row: stock boots until a PLAY power-on re-arms it
 *   STOCK_ONCE "Boot stock once" (DEV): the next boot is stock, then ARMED
 * Healthy = the CFW task ran 20 s, or the session ended through the stock power-off job (clean flag). While the
 * RTC is invalid both wrappers do nothing and S reads ARMED: the CFW runs (fail-open). */
enum { SB_ARMED, SB_ONE_FAIL, SB_DISARMED, SB_STOCK_ONCE };
#define SB_BIT 2
#define SB_HEALTHY_MS 20000u

static inline uint32_t sb_get(void) {
    return (uint32_t)GR2_FN_OPSR_GETB(SB_BIT) | (uint32_t)GR2_FN_OPSR_GETB(SB_BIT + 1) << 1;
}

/* Low bit first: an interrupted write lands on ARMED (1 -> 2) or DISARMED (3 -> 0), never on STOCK_ONCE. */
static inline void sb_set(uint32_t s) {
    GR2_FN_OPSR_SETB(SB_BIT, s & 1);
    GR2_FN_OPSR_SETB(SB_BIT + 1, (s >> 1) & 1);
}

#endif /* SAFEBOOT_H */
