/* ARM-state inner loops of the halation pass (src/halation.c): 16 registers, LDM and conditional execution make
 * them several times cheaper than Thumb-1 code. Called via interworking from the still-processing task;
 * position-independent (the literal is a constant), no stack beyond the saved registers and a small frame. */

    .syntax unified
    .arch armv5te
    .arm
    .text

/* Chroma fold of src/halation.c fold(): each byte c to c - 128 (c >= 128) or 127 - c (c < 128), OR-ed into r3
 * (r12 = 0x80808080; uses r9). */
.macro FOLD w
    bic     r9, r12, \w                 @ 0x80 where c < 128
    sub     r9, r9, r9, lsr #7          @ 0x7F there
    eor     \w, \w, r9
    orr     r3, r3, \w
.endm

/* Box at k + off { ky, kc, yhi } (src/halation.c in_box()): the folded chroma OR (r3) has no bit of kc and no Y
 * byte is above ny: then the block holds no source, next block (label 1). First on the OR of the Y words (r8),
 * which may overstate; then on every Y word of the block (r0 - 32 ..). Uses r4-r7, r9, r10. */
.macro YW w
    bic     r6, \w, r12
    add     r6, r6, r9
    andne   r6, r6, \w                  @ ny >= 128: ((w & 7F) + ky) & w
    orreq   r6, r6, \w                  @ ny < 128: ((w & 7F) + ky) | w
    orr     r7, r7, r6
.endm
.macro BOX off
    ldr     r9, [r11, #\off + 4]        @ kc: chroma bits >= D
    tst     r3, r9
    bne     2f
    ldr     r9, [r11, #\off]            @ ky
    ldr     r10, [r11, #\off + 8]       @ yhi
    cmp     r10, #0
    bicne   r6, r8, r12                 @ ny >= 128: ((y & 7F) + ky) & y
    addne   r6, r6, r9
    andne   r6, r6, r8
    addeq   r6, r8, r9                  @ ny < 128: (y + ky) | y
    orreq   r6, r6, r8
    tst     r6, r12
    addeq   lr, lr, #1
    beq     1b
    ldr     r10, [r11, #\off + 8]       @ yhi again: the flags select the form below
    cmp     r10, #0
    sub     r10, r0, #32
    mov     r7, #0
    .rept 4
    ldmia   r10!, {r4, r5}
    YW      r4
    YW      r5
    .endr
    tst     r7, r12
    addeq   lr, lr, #1
    beq     1b
2:
.endm

/* uint32_t hx_scan4(const uint32_t *y, const uint32_t *cb, const uint32_t *cr, const uint32_t (*k)[4], uint32_t n)
 * k: 4 boxes { ky, kc, yhi, - } (src/halation.c build()). Blocks of 4 8-px groups (8 Y words, 4 Cb and 4 Cr words,
 * consecutive): returns the index of the first of n blocks that may hold a source, n if none. "May": no box holds
 * every byte of the block (src/halation.c hit()). */
    .global hx_scan4
    .type hx_scan4, %function
hx_scan4:
    push    {r4-r11, lr}
    mov     r11, r3
    ldr     r12, =0x80808080
    mov     lr, #0
1:  ldr     r9, [sp, #36]               @ n (5th argument)
    cmp     lr, r9
    bhs     9f
    mov     r3, #0
    ldmia   r1!, {r4-r7}                @ Cb
    FOLD    r4
    FOLD    r5
    FOLD    r6
    FOLD    r7
    ldmia   r2!, {r4-r7}                @ Cr
    FOLD    r4
    FOLD    r5
    FOLD    r6
    FOLD    r7
    ldmia   r0!, {r5-r8}                @ Y 0..3
    orr     r8, r8, r5
    orr     r8, r8, r6
    orr     r8, r8, r7
    ldmia   r0!, {r4-r7}                @ Y 4..7
    orr     r8, r8, r4
    orr     r8, r8, r5
    orr     r8, r8, r6
    orr     r8, r8, r7
    BOX     0
    BOX     16
    BOX     32
    BOX     48
9:  mov     r0, lr
    pop     {r4-r11, pc}
    .ltorg
    .size hx_scan4, . - hx_scan4

/* uint32_t hx_group(hx_kern_t *k, uint32_t *y, uint32_t *cb, uint32_t *cr)
 * One 8-px group of one row of the add-back (src/halation.c addback_rows, which documents the arithmetic): steps
 * the group's 12 halo accumulators (k->hp: Q16 values, then their per-row steps) into k->h (Q4); if none is >= 8,
 * nothing else. Then per pixel and channel the code from Y and the pair's chroma through m and, where
 * h - a' S(code) >= 8, the new code t[(tinv + h - a' S + 8) >> 4] (t has 2048 entries, so no clamp); the code
 * deltas through c into Y (per pixel, clipped) and Cb/Cr (pair mean, clipped). Writes the words that changed;
 * returns how many.
 * k: +0 t[3], +12 ts[3] (tinv << 16 | a'S per code), +24 m[9], +60 c[9], +96 h[3][4] (channel-major, Q4), +144 hp. */
    .equ K_T, 0
    .equ K_TS, 12
    .equ K_M, 24
    .equ K_C, 60
    .equ K_H, 96
    .equ K_HP, 144

@ channel c of the halo: accumulators += steps, h = accumulator >> 16; r12 |= any h >= 8 (h >= 0). r4 = hp.
.macro STEP c
    add     r5, r4, #16 * \c
    ldmia   r5, {r6-r9}
    add     r10, r4, #48 + 16 * \c
    ldmia   r10, {r10, r11}
    add     r6, r6, r10
    add     r7, r7, r11
    add     r10, r4, #56 + 16 * \c
    ldmia   r10, {r10, r11}
    add     r8, r8, r10
    add     r9, r9, r11
    stmia   r5, {r6-r9}
    mov     r6, r6, asr #16
    mov     r7, r7, asr #16
    mov     r8, r8, asr #16
    mov     r9, r9, asr #16
    add     r5, r0, #K_H + 16 * \c
    stmia   r5, {r6-r9}
    orr     r6, r6, r7
    orr     r6, r6, r8
    orr     r6, r6, r9
    orr     r12, r12, r6
.endm
    @ frame
    .equ F_Y, 0                         @ y, cb, cr pointers
    .equ F_CB, 4
    .equ F_CR, 8
    .equ F_W, 12                        @ yw0, yw1, cbw, crw (input)
    .equ F_N, 28                        @ ny0, ny1, ncb, ncr (output)
    .equ F_CH, 44                       @ chroma term per channel (+ 2048)
    .equ F_DCB, 56
    .equ F_DCR, 60
    .equ F_CBV, 64                      @ cb - 128, cr - 128 of the pair
    .equ F_CRV, 68
    .equ F_SIZE, 72

@ d = delta of channel c at pixel Y (r1) of pair j; uses r7-r10
.macro CHAN c, j, d
    ldr     r9, [r0, #K_H + 16 * \c + 4 * \j]
    cmp     r9, #8
    movlt   \d, #0
    blt     1f
    ldr     r7, [r0, #K_M + 12 * \c]
    ldr     r8, [sp, #F_CH + 4 * \c]
    mla     r7, r1, r7, r8
    movs    r7, r7, asr #12
    movmi   r7, #0
    cmp     r7, #255
    movgt   r7, #255
    ldr     r8, [r0, #K_TS + 4 * \c]
    ldr     r8, [r8, r7, lsl #2]
    mov     r10, r8, lsl #16
    sub     r9, r9, r10, lsr #16        @ x = h - a'S
    cmp     r9, #8
    movlt   \d, #0
    blt     1f
    add     r9, r9, r8, lsr #16         @ + tinv
    add     r9, r9, #8
    ldr     r10, [r0, #K_T + 4 * \c]
    ldrb    r10, [r10, r9, lsr #4]
    sub     \d, r10, r7
1:
.endm

@ clamp r to 0..255 (flags from the caller's last op are not used)
.macro CLAMP r
    cmp     \r, #0
    movlt   \r, #0
    cmp     \r, #255
    movgt   \r, #255
.endm

@ pixel e of pair j: Y from input word j / 2, the deltas, the new Y into the output word, Cb/Cr sums
.macro PIXEL j, e
    ldr     r1, [sp, #F_W + 4 * (\j / 2)]
    mov     r1, r1, lsr #(24 - 8 * (2 * (\j % 2) + \e))
    and     r1, r1, #255
    CHAN    0, \j, r4
    CHAN    1, \j, r5
    CHAN    2, \j, r6
    orr     r8, r4, r5                  @ no code changed: Y as it was, no chroma delta
    orrs    r8, r8, r6
    moveq   r8, r1
    beq     2f
    ldr     r9, [sp, #F_DCB]
    ldr     r7, [r0, #K_C + 12]
    mla     r9, r4, r7, r9
    ldr     r7, [r0, #K_C + 16]
    mla     r9, r5, r7, r9
    ldr     r7, [r0, #K_C + 20]
    mla     r9, r6, r7, r9
    str     r9, [sp, #F_DCB]
    ldr     r9, [sp, #F_DCR]
    ldr     r7, [r0, #K_C + 24]
    mla     r9, r4, r7, r9
    ldr     r7, [r0, #K_C + 28]
    mla     r9, r5, r7, r9
    ldr     r7, [r0, #K_C + 32]
    mla     r9, r6, r7, r9
    str     r9, [sp, #F_DCR]
    ldr     r7, [r0, #K_C + 0]
    mul     r8, r4, r7
    ldr     r7, [r0, #K_C + 4]
    mla     r8, r5, r7, r8
    ldr     r7, [r0, #K_C + 8]
    mla     r8, r6, r7, r8
    add     r8, r8, #128
    add     r8, r1, r8, asr #8
    CLAMP   r8
2:  ldr     r9, [sp, #F_N + 4 * (\j / 2)]
    orr     r9, r9, r8, lsl #(24 - 8 * (2 * (\j % 2) + \e))
    str     r9, [sp, #F_N + 4 * (\j / 2)]
.endm

@ pair j: chroma terms, two pixels, the new Cb/Cr bytes
.macro PAIR j
    ldr     r2, [sp, #F_W + 8]
    mov     r2, r2, lsr #(24 - 8 * \j)
    and     r2, r2, #255
    sub     r2, r2, #128
    ldr     r3, [sp, #F_W + 12]
    mov     r3, r3, lsr #(24 - 8 * \j)
    and     r3, r3, #255
    sub     r3, r3, #128
    str     r2, [sp, #F_CBV]
    str     r3, [sp, #F_CRV]
    mov     r11, #2048
    ldr     r7, [r0, #K_M + 4]
    mla     r8, r2, r7, r11
    ldr     r7, [r0, #K_M + 8]
    mla     r8, r3, r7, r8
    str     r8, [sp, #F_CH + 0]
    ldr     r7, [r0, #K_M + 16]
    mla     r8, r2, r7, r11
    ldr     r7, [r0, #K_M + 20]
    mla     r8, r3, r7, r8
    str     r8, [sp, #F_CH + 4]
    ldr     r7, [r0, #K_M + 28]
    mla     r8, r2, r7, r11
    ldr     r7, [r0, #K_M + 32]
    mla     r8, r3, r7, r8
    str     r8, [sp, #F_CH + 8]
    mov     r7, #0
    str     r7, [sp, #F_DCB]
    str     r7, [sp, #F_DCR]
    PIXEL   \j, 0
    PIXEL   \j, 1
    ldr     r8, [sp, #F_DCB]            @ Cb + mean delta
    add     r8, r8, #256
    ldr     r2, [sp, #F_CBV]
    add     r2, r2, #128
    add     r8, r2, r8, asr #9
    CLAMP   r8
    ldr     r9, [sp, #F_N + 8]
    orr     r9, r9, r8, lsl #(24 - 8 * \j)
    str     r9, [sp, #F_N + 8]
    ldr     r8, [sp, #F_DCR]            @ Cr + mean delta
    add     r8, r8, #256
    ldr     r3, [sp, #F_CRV]
    add     r3, r3, #128
    add     r8, r3, r8, asr #9
    CLAMP   r8
    ldr     r9, [sp, #F_N + 12]
    orr     r9, r9, r8, lsl #(24 - 8 * \j)
    str     r9, [sp, #F_N + 12]
.endm

    .global hx_group
    .type hx_group, %function
hx_group:
    push    {r4-r11, lr}
    ldr     r4, [r0, #K_HP]
    mov     r12, #0
    STEP    0
    STEP    1
    STEP    2
    bics    r12, r12, #7
    moveq   r0, #0
    popeq   {r4-r11, pc}
    sub     sp, sp, #F_SIZE
    str     r1, [sp, #F_Y]
    str     r2, [sp, #F_CB]
    str     r3, [sp, #F_CR]
    ldr     r4, [r1]
    ldr     r5, [r1, #4]
    ldr     r6, [r2]
    ldr     r7, [r3]
    add     r8, sp, #F_W
    stmia   r8, {r4-r7}
    mov     r4, #0
    mov     r5, #0
    mov     r6, #0
    mov     r7, #0
    add     r8, sp, #F_N
    stmia   r8, {r4-r7}
    PAIR    0
    PAIR    1
    PAIR    2
    PAIR    3
    mov     r0, #0                      @ write what changed
    add     r8, sp, #F_W
    ldmia   r8, {r4-r7}                 @ old words
    add     r8, sp, #F_N
    ldmia   r8, {r8-r11}                @ new words
    ldr     r1, [sp, #F_Y]
    cmp     r4, r8
    strne   r8, [r1]
    addne   r0, r0, #1
    cmp     r5, r9
    strne   r9, [r1, #4]
    addne   r0, r0, #1
    ldr     r1, [sp, #F_CB]
    cmp     r6, r10
    strne   r10, [r1]
    addne   r0, r0, #1
    ldr     r1, [sp, #F_CR]
    cmp     r7, r11
    strne   r11, [r1]
    addne   r0, r0, #1
    add     sp, sp, #F_SIZE
    pop     {r4-r11, pc}
    .size hx_group, . - hx_group
