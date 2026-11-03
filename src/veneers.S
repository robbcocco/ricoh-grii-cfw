/* Hook veneers at fixed addresses (gr2_boot.ld). Each address has the same low
 * 12 bits as the stock BL target it replaces, so retargeting the stock Thumb
 * BL only changes its first halfword (offset bits 22..12): one atomic 16-bit
 * store, no mixed old/new instruction is ever observable. The BL has already
 * set LR to the stock return address; the veneer tail-jumps to the C hook in
 * stage 2 (address stored in the target word by stage 2 before it patches the
 * BL), which returns straight to the stock caller. r3 is caller-saved at all
 * sites. */

    .syntax unified
    .arch armv5te
    .thumb

    .section .veneer_off, "ax"
    .global cfw_veneer_off
    .type cfw_veneer_off, %function
    .thumb_func
cfw_veneer_off:                      @ replaces bl 0xA00EA1D4 ("*:/Shutdown.ttl") at 0xA00E9950 (power-off job)
    ldr     r3, cfw_veneer_off_target
    bx      r3
    .align 2
    .global cfw_veneer_off_target
cfw_veneer_off_target:
    .word   0
    .size cfw_veneer_off, . - cfw_veneer_off

    .section .veneer_iq, "ax"
    .global cfw_veneer_iq
    .type cfw_veneer_iq, %function
    .thumb_func
cfw_veneer_iq:                       @ replaces bl 0xA01C2A80 at 0xA01B584A (IQ.bin loaded, before LV)
    ldr     r3, cfw_veneer_iq_target
    bx      r3
    .align 2
    .global cfw_veneer_iq_target
cfw_veneer_iq_target:
    .word   0
    .size cfw_veneer_iq, . - cfw_veneer_iq

    .section .veneer_press, "ax"
    .global cfw_veneer_press
    .type cfw_veneer_press, %function
    .thumb_func
cfw_veneer_press:                    @ replaces bl 0xA039607C at 0xA03A1F92
    ldr     r3, cfw_veneer_press_target
    bx      r3
    .align 2
    .global cfw_veneer_press_target
cfw_veneer_press_target:
    .word   0
    .size cfw_veneer_press, . - cfw_veneer_press

    .section .veneer_hold, "ax"
    .global cfw_veneer_hold
    .type cfw_veneer_hold, %function
    .thumb_func
cfw_veneer_hold:                     @ replaces bl 0xA0396EA0 at 0xA03A5790
    ldr     r3, cfw_veneer_hold_target
    bx      r3
    .align 2
    .global cfw_veneer_hold_target
cfw_veneer_hold_target:
    .word   0
    .size cfw_veneer_hold, . - cfw_veneer_hold

#if CFW_DEBUG
    @ Debug only (H1 halation probe, src/fxprobe.c): replaces bl 0xA02AF6CE (still effect step) at 0xA02AEDC0 and
    @ 0xA02AEF30. Its address 0xA001E6CE is 2 mod 4, so the section is only halfword aligned and the literal
    @ is placed by hand: pc reads as 0xA001E6D0 at the ldr, the target word is at 0xA001E6D4 (gr2_boot.ld asserts).
    .section .veneer_fx, "ax"
    .balign 2
    .global cfw_veneer_fx
    .type cfw_veneer_fx, %function
    .thumb_func
cfw_veneer_fx:
    ldr     r3, [pc, #4]
    bx      r3
    nop
    .global cfw_veneer_fx_target
cfw_veneer_fx_target:
    .hword  0, 0
    .size cfw_veneer_fx, . - cfw_veneer_fx
#endif
