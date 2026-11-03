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
 * the group's 24 accumulators (k->hp: Q16 values, then their per-row steps, for the cells' level L and for h2, the
 * halo less k x the cells' hold) into k->h (L), k->h2 (Q4); if no h2 is >= 8, nothing else. Then per chroma pair
 * (its mean Y, its Cb/Cr): the code of each channel through m; x = h2, smooth and the same for a pixel and its
 * darker neighbour (no outline at a lamp's core); x H / (H + x) with H the headroom to the top (it tapers only
 * there, never clips),
 * x fade (the brightest channel's E near the top: none at it, a clipped light keeps its colour); the new code
 * t[(B + x + dither) >> 4] - t[B >> 4] from a base B: the pixel's E, or for a pixel darker than L under a faint glow
 * (x <= L) L itself, blended back to E by x = 2 L (a lit area's dark texture keeps its contrast; a strong glow, a
 * lamp's ring, is each pixel's own); green's code step at most red's, blue's at most green's (no green fringe);
 * dY, Cb, Cr, each pixel's dY clamped to its own luma headroom; if a channel of either pixel would decode darker
 * (rounding, a clamp), dY is raised by the deficit, or without room the pair takes the luma only: every pixel only
 * gains light. A 4 x 4 Bayer rounding per row and pair: no banding. Writes the words that changed; returns how
 * many.
 * k: +0 t[3], +12 ts[3] (tinv << 16 per code), +24 m[9], +60 c[9], +96 L[3][4], +144 h2[3][4] (channel-major, Q4),
 * +192 hp, +196 k[3] (Q12), +208 dither[4] (Q4), +212 fade (u16 per linear E, Q8), +216 rec (u16, 65535 / n). */
    .equ K_T, 0
    .equ K_TS, 12
    .equ K_M, 24
    .equ K_C, 60
    .equ K_H, 96
    .equ K_H2, 144
    .equ K_HP, 192
    .equ K_A, 196
    .equ K_DI, 208
    .equ K_FD, 212
    .equ K_RC, 216
@ channel c of field f (0: the cells' least level L, 1: the halo less the cells' level, h2): accumulators +=
@ steps, value = accumulator >> 16 into k; for h2, r12 |= its positive values. r4 = hp (per field 12
@ accumulators, then 12 steps).
.macro STEP c, f
    add     r5, r4, #96 * \f + 16 * \c
    ldmia   r5, {r6-r9}
    add     r10, r4, #96 * \f + 48 + 16 * \c
    ldmia   r10, {r10, r11}
    add     r6, r6, r10
    add     r7, r7, r11
    add     r10, r4, #96 * \f + 56 + 16 * \c
    ldmia   r10, {r10, r11}
    add     r8, r8, r10
    add     r9, r9, r11
    stmia   r5, {r6-r9}
    mov     r6, r6, asr #16
    mov     r7, r7, asr #16
    mov     r8, r8, asr #16
    mov     r9, r9, asr #16
    add     r5, r0, #K_H + 48 * \f + 16 * \c
    stmia   r5, {r6-r9}
    .if \f == 1
    bic     r6, r6, r6, asr #31
    bic     r7, r7, r7, asr #31
    bic     r8, r8, r8, asr #31
    bic     r9, r9, r9, asr #31
    orr     r6, r6, r7
    orr     r6, r6, r8
    orr     r6, r6, r9
    orr     r12, r12, r6
    .endif
.endm
    @ frame
    .equ F_Y, 0                         @ y, cb, cr pointers
    .equ F_CB, 4
    .equ F_CR, 8
    .equ F_W, 12                        @ yw0, yw1, cbw, crw (input)
    .equ F_N, 28                        @ ny0, ny1, ncb, ncr (output)
    .equ F_CBV, 44                      @ cb - 128, cr - 128 of the pair
    .equ F_CRV, 48
    .equ F_P, 52                        @ ts word per channel
    .equ F_K, 64                        @ code per channel
    .equ F_Y0, 76                       @ Y of its two pixels
    .equ F_Y1, 80
    .equ F_FD, 84                       @ fade of the pair (Q8)
    .equ F_DY, 88                       @ the pair's dY before the no-drop fix
    .equ F_X, 92                        @ a channel's x (CHAN)
    .equ F_B, 96                        @ a channel's base code (CHAN)
    .equ F_SIZE, 108                    @ + 36 pushed: 144, 8-aligned

@ code of channel c at Y r1, chroma r2, r3 (r11 = 2048) -> F_K, its ts word -> F_P; uses r7-r9
.macro CODE c
    ldr     r8, [r0, #K_M + 12 * \c + 4]
    mla     r9, r2, r8, r11
    ldr     r8, [r0, #K_M + 12 * \c + 8]
    mla     r9, r3, r8, r9
    ldr     r7, [r0, #K_M + 12 * \c]
    mla     r9, r1, r7, r9
    movs    r9, r9, asr #12
    movmi   r9, #0
    cmp     r9, #255
    movgt   r9, #255
    str     r9, [sp, #F_K + 4 * \c]
    ldr     r8, [r0, #K_TS + 4 * \c]
    ldr     r8, [r8, r9, lsl #2]
    str     r8, [sp, #F_P + 4 * \c]
.endm

@ d = code delta of channel c for pair j: x = h2 (the halo less k x its cells' hold, smooth), x H / (H + x) (H the
@ headroom: it never reaches the top), x fade; d = t[B + x] - t[B] from a base B: the pixel's E, or, for a pixel
@ darker than L (its cells' mean level, at most twice their least) under a faint glow (x <= L), L itself (a lit
@ area's dark texture keeps its contrast: no speckles), blended back to E as x grows to 2 L (a strong glow, a lamp's
@ ring: each pixel its own step, no outline); uses r7-r10
.macro CHAN c, j, d
    ldr     r9, [r0, #K_H2 + 16 * \c + 4 * \j]
    cmp     r9, #8
    movlt   \d, #0
    blt     1f
    ldr     r8, [sp, #F_P + 4 * \c]
    mov     r10, r8, lsr #16            @ tinv, Q4
    ldr     r7, [r0, #K_H + 16 * \c + 4 * \j]
    ldr     r8, [sp, #F_K + 4 * \c]     @ the base code: the pixel's
    cmp     r10, r7
    bhs     2f
    mov     r8, r7, lsr #6
    cmp     r8, #4
    bhs     6f
    ldr     r8, [sp, #F_K + 4 * \c]
    b       2f
6:  str     r9, [sp, #F_X]
    ldr     r9, [r0, #K_RC]
    add     r9, r9, r8, lsl #1
    ldrh    r9, [r9]                    @ 65535 / (L >> 6)
    ldr     r8, [sp, #F_X]
    mul     r8, r9, r8
    mov     r8, r8, lsr #16             @ 64 x / L
    rsbs    r8, r8, #128                @ the share of L in the base: 128 - 64 x / L, 0 .. 64
    movmi   r8, #0
    cmp     r8, #64
    movhi   r8, #64
    sub     r9, r7, r10
    mul     r9, r8, r9
    add     r10, r10, r9, lsr #6        @ B
    ldr     r9, [sp, #F_X]
    ldr     r8, [r0, #K_T + 4 * \c]
    ldrb    r8, [r8, r10, lsr #4]       @ t[B]
2:  str     r8, [sp, #F_B]
    rsb     r7, r10, #16320             @ headroom H = 16368 - E (Q4)
    add     r7, r7, #48
    add     r8, r7, r9
    movs    r8, r8, lsr #6
    cmp     r8, #4
    bhs     5f
    cmp     r9, r7
    movgt   r9, r7
    b       9f
5:  str     r9, [sp, #F_X]
    ldr     r9, [r0, #K_RC]
    add     r9, r9, r8, lsl #1
    ldrh    r9, [r9]
    mul     r8, r7, r9
    mov     r8, r8, lsr #16
    cmp     r8, #64
    movhi   r8, #64
    ldr     r9, [sp, #F_X]
    mul     r7, r9, r8
    mov     r9, r7, asr #6
9:  ldr     r7, [sp, #F_FD]
    cmp     r7, #256
    mulne   r8, r9, r7
    movne   r9, r8, asr #8              @ x fade
    cmp     r9, #8
    movlt   \d, #0
    blt     1f
    add     r9, r9, r10                 @ + tinv
    ldrb    r7, [r0, #K_DI + \j]        @ + the row's rounding (dither)
    add     r9, r9, r7
    ldr     r10, [r0, #K_T + 4 * \c]
    ldrb    r10, [r10, r9, lsr #4]
    ldr     r7, [sp, #F_B]
    sub     \d, r10, r7
1:
.endm

@ clamp r to 0..255
.macro CLAMP r
    cmp     \r, #0
    movlt   \r, #0
    cmp     \r, #255
    movgt   \r, #255
.endm

@ r8 = dY of the code steps r4-r6 (rounded); uses r7
.macro DY
    ldr     r7, [r0, #K_C + 0]
    mul     r8, r4, r7
    ldr     r7, [r0, #K_C + 4]
    mla     r8, r5, r7, r8
    ldr     r7, [r0, #K_C + 8]
    mla     r8, r6, r7, r8
    add     r8, r8, #128
    mov     r8, r8, asr #8
.endm

@ r6 = min(r6, decoded change of channel c, Q12) for dY r8, dCb r4, dCr r5; uses r7, r9
.macro DEC c
    ldr     r7, [r0, #K_M + 12 * \c]
    mul     r9, r8, r7
    ldr     r7, [r0, #K_M + 12 * \c + 4]
    mla     r9, r4, r7, r9
    ldr     r7, [r0, #K_M + 12 * \c + 8]
    mla     r9, r5, r7, r9
    cmp     r9, r6
    movlt   r6, r9
.endm

@ r = cb (r) or cr: the pair's byte j of input word F_W + off
.macro CBYTE r, off, j
    ldr     \r, [sp, #F_W + \off]
    mov     \r, \r, lsr #(24 - 8 * \j)
    and     \r, \r, #255
.endm

@ pair j: its pixels' Y, the deltas at their mean, the new bytes
.macro PAIR j
    ldr     r4, [r0, #K_H2 + 4 * \j]
    ldr     r5, [r0, #K_H2 + 16 + 4 * \j]
    ldr     r6, [r0, #K_H2 + 32 + 4 * \j]
    bic     r4, r4, r4, asr #31
    bic     r5, r5, r5, asr #31
    bic     r6, r6, r6, asr #31
    orr     r7, r4, r5
    orr     r7, r7, r6
    ldr     r1, [sp, #F_W + 4 * (\j / 2)]
    mov     r8, r1, lsr #(24 - 16 * (\j % 2))
    and     r8, r8, #255
    mov     r9, r1, lsr #(16 - 16 * (\j % 2))
    and     r9, r9, #255
    str     r8, [sp, #F_Y0]
    str     r9, [sp, #F_Y1]
    bics    r7, r7, #7                  @ every h2 < 8 (x <= h2): the pair stays
    beq     3f
    add     r1, r8, r9
    add     r1, r1, #1
    mov     r1, r1, lsr #1              @ the pair's Y
    CBYTE   r2, 8, \j
    CBYTE   r3, 12, \j
    sub     r2, r2, #128
    sub     r3, r3, #128
    str     r2, [sp, #F_CBV]
    str     r3, [sp, #F_CRV]
    mov     r11, #2048
    CODE    0
    CODE    1
    CODE    2
    ldr     r7, [sp, #F_P]              @ the brightest channel's E: the fade at the top
    ldr     r8, [sp, #F_P + 4]
    ldr     r9, [sp, #F_P + 8]
    cmp     r8, r7
    movhi   r7, r8
    cmp     r9, r7
    movhi   r7, r9
    mov     r7, r7, lsr #20             @ E = tinv >> 4
    ldr     r9, [r0, #K_FD]
    add     r9, r9, r7, lsl #1
    ldrh    r9, [r9]
    cmp     r9, #0                      @ at the top: a clipped light keeps its colour
    beq     3f
    str     r9, [sp, #F_FD]
    CHAN    0, \j, r4
    CHAN    1, \j, r5
    CHAN    2, \j, r6
    ldr     r7, [r0, #K_A]
    cmp     r7, #0
    beq     6f
    cmp     r5, r4                      @ green's step at most red's, blue's at most green's: a red channel near
    movgt   r5, r4                      @ the top leaves no green or olive fringe
    cmp     r6, r5
    movgt   r6, r5
6:  orr     r7, r4, r5
    orrs    r7, r7, r6
    beq     3f
    DY
    str     r8, [sp, #F_DY]
    ldr     r7, [r0, #K_C + 12]         @ Cb
    mul     r9, r4, r7
    ldr     r7, [r0, #K_C + 16]
    mla     r9, r5, r7, r9
    ldr     r7, [r0, #K_C + 20]
    mla     r9, r6, r7, r9
    add     r9, r9, #128
    ldr     r2, [sp, #F_CBV]
    add     r2, r2, #128
    add     r2, r2, r9, asr #8
    CLAMP   r2
    ldr     r7, [r0, #K_C + 24]         @ Cr
    mul     r9, r4, r7
    ldr     r7, [r0, #K_C + 28]
    mla     r9, r5, r7, r9
    ldr     r7, [r0, #K_C + 32]
    mla     r9, r6, r7, r9
    add     r9, r9, #128
    ldr     r3, [sp, #F_CRV]
    add     r3, r3, #128
    add     r3, r3, r9, asr #8
    CLAMP   r3
    ldr     r7, [sp, #F_CBV]            @ the decoded change of each channel (rounding, clamps) must not be < 0
    add     r7, r7, #128
    sub     r4, r2, r7
    ldr     r7, [sp, #F_CRV]
    add     r7, r7, #128
    sub     r5, r3, r7
    mov     r6, #0                      @ each pixel's dY clamped to its own luma headroom
    ldr     r8, [sp, #F_DY]
    ldr     r9, [sp, #F_Y0]
    rsb     r9, r9, #255
    cmp     r8, r9
    movgt   r8, r9
    DEC     0
    DEC     1
    DEC     2
    ldr     r8, [sp, #F_DY]
    ldr     r9, [sp, #F_Y1]
    rsb     r9, r9, #255
    cmp     r8, r9
    movgt   r8, r9
    DEC     0
    DEC     1
    DEC     2
    ldr     r8, [sp, #F_DY]
    cmp     r6, #0
    bge     8f
    rsb     r6, r6, #0
    add     r6, r6, #4080
    add     r6, r6, #15
    add     r8, r8, r6, lsr #12         @ dY raised by the deficit
    ldr     r9, [sp, #F_Y0]
    ldr     r7, [sp, #F_Y1]
    cmp     r7, r9
    movhi   r9, r7
    add     r9, r9, r8
    cmp     r9, #255
    ble     8f
    ldr     r8, [sp, #F_DY]             @ no room: luma only
    ldr     r2, [sp, #F_CBV]
    add     r2, r2, #128
    ldr     r3, [sp, #F_CRV]
    add     r3, r3, #128
8:  ldr     r9, [sp, #F_Y0]
    add     r9, r9, r8
    CLAMP   r9
    str     r9, [sp, #F_Y0]
    ldr     r9, [sp, #F_Y1]
    add     r9, r9, r8
    CLAMP   r9
    str     r9, [sp, #F_Y1]
    b       4f
3:  CBYTE   r2, 8, \j                   @ unchanged
    CBYTE   r3, 12, \j
4:  ldr     r10, [sp, #F_N + 4 * (\j / 2)]
    ldr     r9, [sp, #F_Y0]
    orr     r10, r10, r9, lsl #(24 - 16 * (\j % 2))
    ldr     r9, [sp, #F_Y1]
    orr     r10, r10, r9, lsl #(16 - 16 * (\j % 2))
    str     r10, [sp, #F_N + 4 * (\j / 2)]
    ldr     r10, [sp, #F_N + 8]
    orr     r10, r10, r2, lsl #(24 - 8 * \j)
    str     r10, [sp, #F_N + 8]
    ldr     r10, [sp, #F_N + 12]
    orr     r10, r10, r3, lsl #(24 - 8 * \j)
    str     r10, [sp, #F_N + 12]
.endm

    .global hx_group
    .type hx_group, %function
hx_group:
    push    {r4-r11, lr}
    ldr     r4, [r0, #K_HP]
    mov     r12, #0
    STEP    0, 0
    STEP    1, 0
    STEP    2, 0
    STEP    0, 1
    STEP    1, 1
    STEP    2, 1
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


/* void hx_src(hx_src_t *k, uint32_t g, uint32_t e)
 * src/halation.c source_row() for groups g .. e - 1 of one sample row: the first pixel of chroma pairs 1 and 3
 * (Y bytes 2, 6; chroma bytes 1, 3) through m to codes (clamped), tinv to E (Q4); each channel's own E selects its
 * gain (spektrafilm boosts per channel); the sums += E_c gain >> sh9 and the own light += E_c max(gain, 1) >> sh9
 * (channels in fl), the least codes, the count of samples at every channel's top, the decoded rows.
 * k: +0 y, +4 cb, +8 cr (row words), +12 m[9], +48 tinv[3], +60 gain, +64 top[3], +76 sh9, +80 fl (1 R, 2 G,
 * 4 B), +84 acc[3] (u32 per group), +96 npass, +100 nsat, +104 bmin[3] (u8 per group), +116 acc2[3] (own),
 * +128 gunb (the unboosted gain), +132 bw (the cell row's q, 0 .. 64). */
    .equ S_Y, 0
    .equ S_CB, 4
    .equ S_CR, 8
    .equ S_M, 12
    .equ S_TI, 48
    .equ S_GN, 60
    .equ S_TOP, 64
    .equ S_SH, 76
    .equ S_FL, 80
    .equ S_ACC, 84
    .equ S_NP, 96
    .equ S_NS, 100
    .equ S_MN, 104
    .equ S_AC2, 116
    .equ S_GU, 128
    .equ S_BW, 132

@ channel c: code (clamped) of Y r4, cb r5, cr r6 -> \code, E (Q4) -> \e; uses r3
.macro SCH c, code, e
    ldr     r3, [r0, #S_M + 12 * \c]
    mul     \code, r4, r3
    ldr     r3, [r0, #S_M + 12 * \c + 4]
    mla     \code, r5, r3, \code
    ldr     r3, [r0, #S_M + 12 * \c + 8]
    mla     \code, r6, r3, \code
    add     \code, \code, #2048
    movs    \code, \code, asr #12
    movmi   \code, #0
    cmp     \code, #255
    movgt   \code, #255
    ldr     r3, [r0, #S_TI + 4 * \c]
    add     r3, r3, \code, lsl #1
    ldrh    \e, [r3]
.endm

@ least code: frame slot off = min(slot, \code); uses r3
.macro SMIN code, off
    ldr     r3, [sp, #\off]
    cmp     \code, r3
    strlo   \code, [sp, #\off]
.endm

@ sample of pair j (1 or 3) of group r1: Y word w (0: pixels 0-3, 4: 4-7), chroma shift sc
.macro SAMP w, sc
    ldr     r3, [r0, #S_Y]
    add     r3, r3, r1, lsl #3
    ldr     r3, [r3, #\w]
    mov     r4, r3, lsr #8
    and     r4, r4, #255                @ Y
    ldr     r3, [r0, #S_CB]
    ldr     r3, [r3, r1, lsl #2]
    .if \sc
    mov     r5, r3, lsr #\sc
    .else
    mov     r5, r3
    .endif
    and     r5, r5, #255
    sub     r5, r5, #128                @ cb
    ldr     r3, [r0, #S_CR]
    ldr     r3, [r3, r1, lsl #2]
    .if \sc
    mov     r6, r3, lsr #\sc
    .else
    mov     r6, r3
    .endif
    and     r6, r6, #255
    sub     r6, r6, #128                @ cr
    SCH     0, r7, r8                   @ R, E_r
    SCH     1, r9, r10                  @ G, E_g
    SCH     2, lr, r4                   @ B, E_b (Y no longer needed)
    ldr     r3, [r0, #S_TOP]            @ at every channel's top: nothing can change it
    cmp     r7, r3
    ldrge   r3, [r0, #S_TOP + 4]
    cmpge   r9, r3
    ldrge   r3, [r0, #S_TOP + 8]
    cmpge   lr, r3
    addge   r12, r12, #1
    SMIN    r7, 12
    SMIN    r9, 16
    SMIN    lr, 20
    SACC    r8, 0, 1
    SACC    r10, 4, 2
    SACC    r4, 8, 4
.endm

@ channel with E \e (Q4): its source gain (boosted, blended with the unboosted by the group's share q r11: a
@ large bright area is not a lamp); sum slot off += E gain >> sh9 and own slot off + 24 += E >> sh (its light,
@ unboosted: the cells' level), if the channel has a source (fl bit); uses r3, r5, r6
.macro SACC e, off, bit
    ldr     r3, [r0, #S_FL]
    tst     r3, #\bit
    beq     4f
    mov     r5, \e, lsr #4
    ldr     r3, [r0, #S_GN]
    add     r3, r3, r5, lsl #1
    ldrh    r6, [r3]                    @ gain
    cmp     r11, #0
    beq     5f
    ldr     r3, [r0, #S_GU]             @ blended with the unboosted
    add     r3, r3, r5, lsl #1
    ldrh    r3, [r3]
    rsb     r5, r11, #64                @ blended
    mul     r6, r5, r6
    mla     r6, r3, r11, r6
    add     r6, r6, #32
    mov     r6, r6, lsr #6
5:  mul     r5, \e, r6
    ldr     r3, [sp, #\off]
    ldr     r6, [r0, #S_SH]
    add     r3, r3, r5, lsr r6
    str     r3, [sp, #\off]
    sub     r6, r6, #9
    ldr     r3, [sp, #\off + 24]
    add     r3, r3, \e, lsr r6
    str     r3, [sp, #\off + 24]
4:
.endm

    .global hx_src
    .type hx_src, %function
hx_src:
    push    {r4-r11, lr}
    sub     sp, sp, #44                 @ 36 + 44 = 80, 8-aligned; +0 sums, +12 least codes, +24 own, +36 temp
    cmp     r1, r2
    bhs     9f
1:  ldr     r3, [r0, #S_NP]             @ the group: its sums, least codes, counts
    ldrb    r4, [r3, r1]
    add     r4, r4, #1
    strb    r4, [r3, r1]
    ldr     r3, [r0, #S_NS]
    ldrb    r12, [r3, r1]
    ldr     r3, [r0, #S_BW]
    ldrb    r11, [r3, r1, lsr #1]       @ q (per 2 cells)
    ldr     r3, [r0, #S_ACC]
    ldr     r4, [r3, r1, lsl #2]
    ldr     r3, [r0, #S_ACC + 4]
    ldr     r5, [r3, r1, lsl #2]
    ldr     r3, [r0, #S_ACC + 8]
    ldr     r6, [r3, r1, lsl #2]
    stmia   sp, {r4-r6}
    ldr     r3, [r0, #S_AC2]
    ldr     r4, [r3, r1, lsl #2]
    ldr     r3, [r0, #S_AC2 + 4]
    ldr     r5, [r3, r1, lsl #2]
    ldr     r3, [r0, #S_AC2 + 8]
    ldr     r6, [r3, r1, lsl #2]
    add     r3, sp, #24
    stmia   r3, {r4-r6}
    ldr     r3, [r0, #S_MN]
    ldrb    r4, [r3, r1]
    ldr     r3, [r0, #S_MN + 4]
    ldrb    r5, [r3, r1]
    ldr     r3, [r0, #S_MN + 8]
    ldrb    r6, [r3, r1]
    add     r3, sp, #12
    stmia   r3, {r4-r6}
    SAMP    0, 16                       @ pair 1: Y byte 2, chroma byte 1
    SAMP    4, 0                        @ pair 3: Y byte 6, chroma byte 3
    ldr     r3, [r0, #S_NS]
    strb    r12, [r3, r1]
    ldmia   sp, {r4-r6}
    ldr     r3, [r0, #S_ACC]
    str     r4, [r3, r1, lsl #2]
    ldr     r3, [r0, #S_ACC + 4]
    str     r5, [r3, r1, lsl #2]
    ldr     r3, [r0, #S_ACC + 8]
    str     r6, [r3, r1, lsl #2]
    add     r3, sp, #24
    ldmia   r3, {r4-r6}
    ldr     r3, [r0, #S_AC2]
    str     r4, [r3, r1, lsl #2]
    ldr     r3, [r0, #S_AC2 + 4]
    str     r5, [r3, r1, lsl #2]
    ldr     r3, [r0, #S_AC2 + 8]
    str     r6, [r3, r1, lsl #2]
    add     r3, sp, #12
    ldmia   r3, {r4-r6}
    ldr     r3, [r0, #S_MN]
    strb    r4, [r3, r1]
    ldr     r3, [r0, #S_MN + 4]
    strb    r5, [r3, r1]
    ldr     r3, [r0, #S_MN + 8]
    strb    r6, [r3, r1]
    add     r1, r1, #1
    cmp     r1, r2
    blo     1b
9:  add     sp, sp, #44
    pop     {r4-r11, pc}
    .size hx_src, . - hx_src
