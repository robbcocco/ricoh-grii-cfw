/* Bare-metal ARM/Thumb Entrypoint for Ricoh GR II Custom Firmware
 * Target execution base: 0xA0700000
 */

    .syntax unified
    .arch armv7-a
    .thumb

    .section .text.entry, "ax"
    .global _start
    .type _start, %function
    .thumb_func

_start:
    /* Preserve caller registers and return address */
    push    {r4-r7, lr}

    /* Zero out BSS section */
    ldr     r0, =__bss_start__
    ldr     r1, =__bss_end__
    movs    r2, #0
1:
    cmp     r0, r1
    bge     2f
    str     r2, [r0]
    adds    r0, r0, #4
    b       1b

2:
    /* Call C main entrypoint */
    bl      cfw_main

    /* Return to RTOS caller */
    pop     {r4-r7, pc}

    .size _start, . - _start
