/* ARM-state helper: MRS is not available in Thumb-1. Called via interworking.
 * The shell/CFW tasks run in USR mode (CPSR 0x60000010 on hardware), so
 * privileged CP15 operations (cache maintenance) are not available. */

    .syntax unified
    .arch armv5te
    .arm
    .text

/* uint32_t cfw_cpsr(void) */
    .global cfw_cpsr
    .type cfw_cpsr, %function
cfw_cpsr:
    mrs     r0, cpsr
    bx      lr
    .size cfw_cpsr, . - cfw_cpsr
