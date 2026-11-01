/* Flashed loader stub for the GR II APP image (fw 3.00), M10. NOT FLASHED YET.
 *
 * Two patches in APP section 0 (b01firm8; RAM = 0xA0020200 + image offset), both
 * inside the range the IPL loads (section 0 = 0xA0020400 + 0xA46C4 B):
 *   .ldr_hook  @ 0xA0022E5C  `bl Section_Load` (stock F01C FE51) -> `bl ldr_stub`
 *   .ldr_stub  @ 0xA00AFCF0  132 B of unreferenced 0xFF (stub, 0xFF fill, status word)
 * scripts/build_loader_frm.py assembles this file and patches rg2_v003.frm.
 *
 * The hook site is in FUN_a0022e28(n), run by the root init task 0xA0020944 for
 * every APP section n that is not loaded yet (shooting mode: 1 4 2 3 5 10 11 8 9 6
 * 12 13; section 1 is always first). Arguments: r0 = n, r1 = 0; the caller ignores
 * the result and keeps r4-r7. Stage 0 (GlobalHeap, RTOS task module, File_Init)
 * has run; H: is not mounted yet. See README.md.
 *
 * First call only (status word 0): decide once and record the outcome.
 *   shutter half-press held (RL1, pin 0x27 = P6.1, active low) -> ST_SKIP
 *   File_MountMedia(H:...) returns -1 (no card, mount error)     -> ST_NOMOUNT
 *   LoadFile(H:/cfw/gr2_cfw.bin -> 0xA0017000, max 0x8000) <= 0  -> ST_NOFILE
 *   word +4 != LDR_MAGIC (stage 1 without the loader ABI)       -> ST_NOABI
 *   otherwise                                                   -> ST_ARMED
 * Every call: stock section load, then, if ST_ARMED, call the stage-1 hook
 *   (word +8, Thumb) with r0 = n, r1 = &ldr_state. Stage 1 may write another
 *   value to *r1 to stop further calls.
 * Anything but ST_ARMED leaves the boot stock apart from the H: mount and the
 * window contents (the IPL copy's erased tail, unused by the APP).
 */
    .syntax unified
    .arch armv5te
    .thumb

    .equ Port_Get,        0xA00408D8   /* int Port_Get(pin): pin level 0/1 */
    .equ File_MountMedia, 0xA002D350   /* int File_MountMedia(path): -1 = failed */
    .equ LoadFile,        0xA002CD2C   /* int LoadFile(path, dst, max): bytes or -1 */
    .equ Section_Load,    0xA003FB02   /* stock target of the hooked BL */

    .equ CFW_BASE,  0xA0017000          /* stage-1 window, same address as fload */
    .equ CFW_MAX,   0x8000
    .equ PIN_RL1,   0x27
    .equ LDR_MAGIC, 0x47524C31          /* "GRL1" at stage 1 +4: loader ABI v1 */

    .equ ST_SKIP,    1
    .equ ST_NOMOUNT, 2
    .equ ST_NOFILE,  3
    .equ ST_NOABI,   4
    .equ ST_ARMED,   5

    .section .ldr_hook, "ax"
    bl      ldr_stub                    /* replaces `bl Section_Load` at 0xA0022E5C */

    .section .ldr_stub, "ax"
    .global ldr_stub
    .type ldr_stub, %function
    .thumb_func
ldr_stub:
    push    {r3-r7, lr}                 /* 6 words: SP stays 8-byte aligned */
    movs    r4, r0                      /* n (r1 is always 0 at the hook site) */
    adr     r6, .Lstate
    ldr     r0, [r6]
    cmp     r0, #0
    bne     .Lload                      /* decided on an earlier call */

    movs    r7, #ST_SKIP
    movs    r0, #PIN_RL1
    bl      Port_Get
    cmp     r0, #0
    beq     .Ldecided                   /* half-press held: stay stock */

    movs    r7, #ST_NOMOUNT
    adr     r0, cfw_path
    bl      File_MountMedia             /* nothing has mounted H: yet (README) */
    adds    r0, r0, #1
    beq     .Ldecided                   /* -1: no card / mount failed */

    movs    r7, #ST_NOFILE
    adr     r0, cfw_path
    ldr     r1, =CFW_BASE
    movs    r2, #(CFW_MAX >> 8)
    lsls    r2, r2, #8
    bl      LoadFile
    cmp     r0, #0
    ble     .Ldecided                   /* -1 or empty */

    movs    r7, #ST_NOABI
    ldr     r1, =CFW_BASE
    ldr     r0, [r1, #4]
    ldr     r2, =LDR_MAGIC
    cmp     r0, r2
    bne     .Ldecided                   /* stage 1 without loader ABI: never run it */
    movs    r7, #ST_ARMED
.Ldecided:
    str     r7, [r6]

.Lload:
    movs    r0, r4
    movs    r1, #0
    bl      Section_Load                /* the stock call: Section_Load(n, 0) */
    ldr     r1, [r6]
    cmp     r1, #ST_ARMED
    bne     .Lret
    movs    r5, r0                      /* keep the stock result */
    ldr     r1, =CFW_BASE
    ldr     r3, [r1, #8]
    movs    r0, r4
    movs    r1, r6
    blx     r3                          /* stage-1 hook(n, &ldr_state) */
    movs    r0, r5
.Lret:
    pop     {r3-r7, pc}

    .ltorg
cfw_path:
    .asciz  "H:/cfw/gr2_cfw.bin"

    .org    0x80, 0xFF                  /* unused bytes stay 0xFF like stock */
    .global ldr_state
ldr_state:                              /* 0xA00AFD70: 0 at power-on, then ST_* */
.Lstate:
    .word   0
    .size ldr_stub, . - ldr_stub
