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
