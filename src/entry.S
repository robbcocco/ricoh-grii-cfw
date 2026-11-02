/* Stage-1 Thumb entrypoint for Ricoh GR II Custom Firmware.
 * Two ways in (link base gr2_boot.ld, window 0xA0017000):
 *  - `ex 0xA0017001` from H:/Startup.ttl (Thumb-1 via blx): _start zeroes stage
 *    1's .bss and calls cfw_boot (src/boot.c).
 *  - A flashed loader stub (loader/flash: the debug loader from H:/cfw, the release
 *    firmware from A:/cfw): it loads this file into the window, checks the loader
 *    ABI v1 header below (+4 magic "GRL1") and calls the Thumb entry at +8 after
 *    every section load (cfw_ldr_hook, src/boot.c).
 * +12/+16: image length and CRC-32 for stage 1's self-check (include/boot.h),
 * written after linking by scripts/stage1_crc.py.
 */

    .syntax unified
    .arch armv5te
    .thumb

    .section .text.entry, "ax"
    .global _start
    .type _start, %function
    .thumb_func

_start:
    b       1f                       @ +0: over the loader ABI header
    .hword  0
    .word   0x47524C31               @ +4: "GRL1", loader ABI v1
    .word   cfw_ldr_hook             @ +8: Thumb entry, called as hook(n, &ldr_state)
    .word   0                        @ +12: image length (scripts/stage1_crc.py)
    .word   0                        @ +16: CRC-32 over [0, 16) + [20, length)

1:
    /* Preserve callee-saved registers; 6 words keeps SP 8-byte aligned (AAPCS) */
    push    {r3-r7, lr}

    /* Zero out BSS section */
    ldr     r0, =__bss_start__
    ldr     r1, =__bss_end__
    movs    r2, #0
2:
    cmp     r0, r1
    bhs     3f
    str     r2, [r0]
    adds    r0, r0, #4
    b       2b

3:
    /* Load and start stage 2 */
    bl      cfw_boot

    /* Return to RTOS caller (pop {pc} interworks on ARMv5T) */
    pop     {r3-r7, pc}

    .size _start, . - _start
