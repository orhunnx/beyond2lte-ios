/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 beyond2lte-ios contributors
 * Original scalar AArch64 QARMA5 implementation from the public mathematical
 * construction; same BSD-2-Clause terms as b2l_pacga_qarma5.c.
 * No x18, SIMD/FP, pointer-authentication instructions, or system-register use.
 * Generated fixed cell permutations avoid input-dependent loads and branches.
 */
#if defined(__APPLE__)
#define ENTRY _b2l_pacga_qarma5_asm
#define LOAD_CONSTANTS(reg) adrp reg, Lconstants@PAGE; add reg, reg, Lconstants@PAGEOFF
#else
#define ENTRY b2l_pacga_qarma5_asm
#define LOAD_CONSTANTS(reg) adrp reg, Lconstants; add reg, reg, :lo12:Lconstants
#endif
.text
.p2align 2
.globl ENTRY
ENTRY:
    stp x29, x30, [sp, #-80]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    mov x19, x0
    mov x20, x1
    mov x21, x2
    mov x22, x3
    ror x23, x3, #1
    eor x23, x23, x3, lsr #63
    eor x19, x19, x22
    mov x24, #0
    LOAD_CONSTANTS(x25)
Lforward:
    eor x19, x19, x21
    eor x19, x19, x20
    ldr x0, [x25, x24, lsl #3]
    eor x19, x19, x0
    cbz x24, Lforward_short
    mov x0, x19
    bl Ltau
    bl Lmix
    mov x19, x0
Lforward_short:
    mov x0, x19
    bl Lsigma
    mov x19, x0
    mov x0, x20
    bl Ltweak
    mov x20, x0
    add x24, x24, #1
    cmp x24, #5
    b.lo Lforward
    eor x0, x19, x23
    eor x0, x0, x20
    bl Ltau
    bl Lmix
    bl Lsigma
    bl Ltau
    bl Lmix
    eor x0, x0, x21
    bl Linvtau
    bl Linvsigma
    bl Lmix
    bl Linvtau
    eor x19, x0, x22
    eor x19, x19, x20
    mov x24, #4
    ldr x26, [x25, #40]
Lbackward:
    mov x0, x19
    bl Linvsigma
    cbz x24, Lbackward_short
    bl Lmix
    bl Linvtau
Lbackward_short:
    mov x19, x0
    mov x0, x20
    bl Linvtweak
    mov x20, x0
    eor x19, x19, x21
    eor x19, x19, x20
    ldr x0, [x25, x24, lsl #3]
    eor x19, x19, x0
    eor x19, x19, x26
    subs x24, x24, #1
    b.pl Lbackward
    eor x0, x19, x23
    and x0, x0, #0xffffffff00000000
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x29, x30, [sp], #80
    ret
/* Helpers are leaf functions; they clobber x0-x7 and NZCV only. */
Lmix:
    and x1, x0, #0x7777777777777777
    lsl x1, x1, #1
    and x2, x0, #0x8888888888888888
    orr x1, x1, x2, lsr #3
    and x2, x0, #0x3333333333333333
    lsl x2, x2, #2
    and x3, x0, #0xcccccccccccccccc
    orr x2, x2, x3, lsr #2
    ror x0, x1, #16
    eor x0, x0, x1, ror #48
    eor x0, x0, x2, ror #32
    ret
Ltau:
    mov x1, #0
    ubfx x2, x0, #52, #4
    orr x1, x1, x2, lsl #0
    ubfx x2, x0, #24, #4
    orr x1, x1, x2, lsl #4
    ubfx x2, x0, #44, #4
    orr x1, x1, x2, lsl #8
    ubfx x2, x0, #0, #4
    orr x1, x1, x2, lsl #12
    ubfx x2, x0, #28, #4
    orr x1, x1, x2, lsl #16
    ubfx x2, x0, #48, #4
    orr x1, x1, x2, lsl #20
    ubfx x2, x0, #4, #4
    orr x1, x1, x2, lsl #24
    ubfx x2, x0, #40, #4
    orr x1, x1, x2, lsl #28
    ubfx x2, x0, #32, #4
    orr x1, x1, x2, lsl #32
    ubfx x2, x0, #12, #4
    orr x1, x1, x2, lsl #36
    ubfx x2, x0, #56, #4
    orr x1, x1, x2, lsl #40
    ubfx x2, x0, #20, #4
    orr x1, x1, x2, lsl #44
    ubfx x2, x0, #8, #4
    orr x1, x1, x2, lsl #48
    ubfx x2, x0, #36, #4
    orr x1, x1, x2, lsl #52
    ubfx x2, x0, #16, #4
    orr x1, x1, x2, lsl #56
    ubfx x2, x0, #60, #4
    orr x1, x1, x2, lsl #60
    mov x0, x1
    ret
Linvtau:
    mov x1, #0
    ubfx x2, x0, #12, #4
    orr x1, x1, x2, lsl #0
    ubfx x2, x0, #24, #4
    orr x1, x1, x2, lsl #4
    ubfx x2, x0, #48, #4
    orr x1, x1, x2, lsl #8
    ubfx x2, x0, #36, #4
    orr x1, x1, x2, lsl #12
    ubfx x2, x0, #56, #4
    orr x1, x1, x2, lsl #16
    ubfx x2, x0, #44, #4
    orr x1, x1, x2, lsl #20
    ubfx x2, x0, #4, #4
    orr x1, x1, x2, lsl #24
    ubfx x2, x0, #16, #4
    orr x1, x1, x2, lsl #28
    ubfx x2, x0, #32, #4
    orr x1, x1, x2, lsl #32
    ubfx x2, x0, #52, #4
    orr x1, x1, x2, lsl #36
    ubfx x2, x0, #28, #4
    orr x1, x1, x2, lsl #40
    ubfx x2, x0, #8, #4
    orr x1, x1, x2, lsl #44
    ubfx x2, x0, #20, #4
    orr x1, x1, x2, lsl #48
    ubfx x2, x0, #0, #4
    orr x1, x1, x2, lsl #52
    ubfx x2, x0, #40, #4
    orr x1, x1, x2, lsl #56
    ubfx x2, x0, #60, #4
    orr x1, x1, x2, lsl #60
    mov x0, x1
    ret
Lsigma:
    movz x3, #0xf86b, lsl #0
    movk x3, #0xe90c, lsl #16
    movk x3, #0x5473, lsl #32
    movk x3, #0xa12d, lsl #48
    mov x1, #0
    ubfx x2, x0, #0, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #0
    ubfx x2, x0, #4, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #4
    ubfx x2, x0, #8, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #8
    ubfx x2, x0, #12, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #12
    ubfx x2, x0, #16, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #16
    ubfx x2, x0, #20, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #20
    ubfx x2, x0, #24, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #24
    ubfx x2, x0, #28, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #28
    ubfx x2, x0, #32, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #32
    ubfx x2, x0, #36, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #36
    ubfx x2, x0, #40, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #40
    ubfx x2, x0, #44, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #44
    ubfx x2, x0, #48, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #48
    ubfx x2, x0, #52, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #52
    ubfx x2, x0, #56, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #56
    ubfx x2, x0, #60, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #60
    mov x0, x1
    ret
Linvsigma:
    movz x3, #0x8de5, lsl #0
    movk x3, #0x91ba, lsl #16
    movk x3, #0x0f62, lsl #32
    movk x3, #0x37c4, lsl #48
    mov x1, #0
    ubfx x2, x0, #0, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #0
    ubfx x2, x0, #4, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #4
    ubfx x2, x0, #8, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #8
    ubfx x2, x0, #12, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #12
    ubfx x2, x0, #16, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #16
    ubfx x2, x0, #20, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #20
    ubfx x2, x0, #24, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #24
    ubfx x2, x0, #28, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #28
    ubfx x2, x0, #32, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #32
    ubfx x2, x0, #36, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #36
    ubfx x2, x0, #40, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #40
    ubfx x2, x0, #44, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #44
    ubfx x2, x0, #48, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #48
    ubfx x2, x0, #52, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #52
    ubfx x2, x0, #56, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #56
    ubfx x2, x0, #60, #4
    lsl x2, x2, #2
    lsrv x2, x3, x2
    and x2, x2, #15
    orr x1, x1, x2, lsl #60
    mov x0, x1
    ret
Ltweak:
    mov x1, #0
    ubfx x2, x0, #16, #4
    orr x1, x1, x2, lsl #0
    ubfx x2, x0, #20, #4
    orr x1, x1, x2, lsl #4
    ubfx x2, x0, #24, #4
    orr x1, x1, x2, lsl #8
    ubfx x2, x0, #28, #4
    orr x1, x1, x2, lsl #12
    ubfx x2, x0, #44, #4
    orr x1, x1, x2, lsl #16
    ubfx x2, x0, #8, #4
    orr x1, x1, x2, lsl #20
    ubfx x2, x0, #12, #4
    orr x1, x1, x2, lsl #24
    ubfx x2, x0, #32, #4
    orr x1, x1, x2, lsl #28
    ubfx x2, x0, #48, #4
    orr x1, x1, x2, lsl #32
    ubfx x2, x0, #52, #4
    orr x1, x1, x2, lsl #36
    ubfx x2, x0, #56, #4
    orr x1, x1, x2, lsl #40
    ubfx x2, x0, #60, #4
    orr x1, x1, x2, lsl #44
    ubfx x2, x0, #0, #4
    orr x1, x1, x2, lsl #48
    ubfx x2, x0, #4, #4
    orr x1, x1, x2, lsl #52
    ubfx x2, x0, #40, #4
    orr x1, x1, x2, lsl #56
    ubfx x2, x0, #36, #4
    orr x1, x1, x2, lsl #60
    mov x0, x1
    movz x3, #0x0f00, lsl #0
    movk x3, #0xf00f, lsl #16
    movk x3, #0xf000, lsl #32
    movk x3, #0xff0f, lsl #48
    and x1, x0, x3
    and x2, x1, #0xeeeeeeeeeeeeeeee
    lsr x2, x2, #1
    eor x1, x1, x1, lsr #1
    and x1, x1, #0x1111111111111111
    orr x1, x2, x1, lsl #3
    bic x0, x0, x3
    orr x0, x0, x1
    ret
Linvtweak:
    movz x3, #0x0f00, lsl #0
    movk x3, #0xf00f, lsl #16
    movk x3, #0xf000, lsl #32
    movk x3, #0xff0f, lsl #48
    and x1, x0, x3
    lsl x2, x1, #1
    and x2, x2, #0xeeeeeeeeeeeeeeee
    eor x1, x1, x1, lsr #3
    and x1, x1, #0x1111111111111111
    orr x1, x1, x2
    bic x0, x0, x3
    orr x0, x0, x1
    mov x1, #0
    ubfx x2, x0, #48, #4
    orr x1, x1, x2, lsl #0
    ubfx x2, x0, #52, #4
    orr x1, x1, x2, lsl #4
    ubfx x2, x0, #20, #4
    orr x1, x1, x2, lsl #8
    ubfx x2, x0, #24, #4
    orr x1, x1, x2, lsl #12
    ubfx x2, x0, #0, #4
    orr x1, x1, x2, lsl #16
    ubfx x2, x0, #4, #4
    orr x1, x1, x2, lsl #20
    ubfx x2, x0, #8, #4
    orr x1, x1, x2, lsl #24
    ubfx x2, x0, #12, #4
    orr x1, x1, x2, lsl #28
    ubfx x2, x0, #28, #4
    orr x1, x1, x2, lsl #32
    ubfx x2, x0, #60, #4
    orr x1, x1, x2, lsl #36
    ubfx x2, x0, #56, #4
    orr x1, x1, x2, lsl #40
    ubfx x2, x0, #16, #4
    orr x1, x1, x2, lsl #44
    ubfx x2, x0, #32, #4
    orr x1, x1, x2, lsl #48
    ubfx x2, x0, #36, #4
    orr x1, x1, x2, lsl #52
    ubfx x2, x0, #40, #4
    orr x1, x1, x2, lsl #56
    ubfx x2, x0, #44, #4
    orr x1, x1, x2, lsl #60
    mov x0, x1
    ret

.p2align 3
Lconstants:
    .quad 0x0000000000000000
    .quad 0x13198a2e03707344
    .quad 0xa4093822299f31d0
    .quad 0x082efa98ec4e6c89
    .quad 0x452821e638d01377
    .quad 0xc0ac29b7c97c50dd
#if !defined(__APPLE__)
.section .note.GNU-stack,"",%progbits
#endif
