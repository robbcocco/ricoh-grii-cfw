/* Release loader stub for the GR II APP image (fw 3.00), M11: stage 1 from the A: drive.
 *
 * Same hook site, free range and status word as the debug loader (ldr_stub.S v2):
 *   .ldr_hook  @ 0xA0022E5C  `bl Section_Load` (stock F01C FE51) -> `bl ldr_stub`
 *   .ldr_stub  @ 0xA00AFCF0  132 B of unreferenced 0xFF (stub, 0xFF fill, status word)
 * Placed by ldr_stub.ld; scripts/build_cfw_frm.py assembles this file and builds
 * dist/rg2_v003_cfw.frm (RES carries A:/cfw/..., README.md §8).
 *
 * The only difference to ldr_stub.S v2: stage 1 comes from A: (the RES partition, read
 * raw from NAND with no mount, cache or checksum, finding 29), so there is no
 * File_MountMedia and no ST_NOMOUNT, and a boot without a card or over USB loads the
 * CFW as well. Nothing here touches the card. The decision still waits for section 3
 * (finding 33 speed rule: nothing useful runs earlier; a USB-charge boot loads only
 * section 1 and never decides).
 *
 * First call with n == 3 (status word 0): decide once.
 *   shutter half-press held (RL1, pin 0x27 = P6.1, active low) -> ST_SKIP
 *   LoadFile(A:/cfw/gr2_cfw.bin -> 0xA0017000, max 0x8000) <= 0  -> ST_NOFILE
 *   word +4 != LDR_MAGIC (stage 1 without the loader ABI)       -> ST_NOABI
 *   otherwise                                                   -> ST_ARMED
 * Then, if ST_ARMED, call the stage-1 hook (word +8, Thumb) with r0 = n,
 *   r1 = &ldr_state, after every section load from n = 3 on. LoadFile truncates a
 *   larger file silently: stage 1 checks its own length/CRC header at its first hook
 *   and writes 6 (disarmed) on a mismatch.
 */
    .syntax unified
    .arch armv5te
    .thumb

    .equ Port_Get,        0xA00408D8   /* int Port_Get(pin): pin level 0/1 */
    .equ LoadFile,        0xA002CD2C   /* int LoadFile(path, dst, max): bytes or -1 */
    .equ Section_Load,    0xA003FB02   /* stock target of the hooked BL */

    .equ CFW_BASE,  0xA0017000          /* stage-1 window */
    .equ CFW_MAX,   0x8000
    .equ PIN_RL1,   0x27
    .equ LDR_MAGIC, 0x47524C31          /* "GRL1" at stage 1 +4: loader ABI v1 */

    .equ LDR_DECIDE_SECTION, 3
    .equ ST_SKIP,    1
    .equ ST_NOFILE,  3                  /* 2 (ST_NOMOUNT) is unused: A: needs no mount */
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
    movs    r4, r0                      /* n; r1 = 0 from the hook site */
    bl      Section_Load                /* the stock call first: section n is resident */
    adr     r6, .Lstate                 /* the result is dead: the caller does movs r0,r7 */
    ldr     r0, [r6]
    cmp     r0, #0
    bne     .Lhook                      /* decided on an earlier call */
    cmp     r4, #LDR_DECIDE_SECTION
    bne     .Lret

    movs    r7, #ST_SKIP
    movs    r0, #PIN_RL1
    bl      Port_Get
    cmp     r0, #0
    beq     .Ldecided                   /* half-press held: stay stock */

    movs    r7, #ST_NOFILE
    adr     r0, cfw_path
    ldr     r1, =CFW_BASE
    movs    r2, #(CFW_MAX >> 8)
    lsls    r2, r2, #8
    bl      LoadFile                    /* A:: NAND only, no mount */
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

.Lhook:
    ldr     r1, [r6]
    cmp     r1, #ST_ARMED
    bne     .Lret
    ldr     r1, =CFW_BASE
    ldr     r3, [r1, #8]
    movs    r0, r4
    movs    r1, r6
    blx     r3                          /* stage-1 hook(n, &ldr_state) */
.Lret:
    pop     {r3-r7, pc}

    .ltorg
cfw_path:
    .asciz  "A:/cfw/gr2_cfw.bin"

    .org    0x80, 0xFF                  /* unused bytes stay 0xFF like stock */
    .global ldr_state
ldr_state:                              /* 0xA00AFD70: 0 at power-on, then ST_* */
.Lstate:
    .word   0
    .size ldr_stub, . - ldr_stub
