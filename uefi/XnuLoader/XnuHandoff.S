/* SPDX-License-Identifier: BSD-2-Clause */
#include <AsmMacroLib.h>
.text
/* Progress square for one pre-labelled row. GOP is firmware WRITE_THROUGH.
 * All addresses are physical/identity. No stack, library or EFI calls. */
.macro B2L_STAGE index, color
    cbz x21, .Lstage_skip\@
    mov x14, #(\index * 96)
    madd x14, x14, x22, x21
    mov x15, #\color
    mov x16, #64
.Lstage_row\@:
    mov x18, x14
    mov x17, #128
.Lstage_col\@:
    str w15, [x18], #4
    subs x17, x17, #1
    b.ne .Lstage_col\@
    add x14, x14, x22
    subs x16, x16, #1
    b.ne .Lstage_row\@
    dsb sy
.Lstage_skip\@:
.endm
/* x0 entry PA, x1 boot_args PA, x2 struct b2l_handoff_info.
 * Unlike the first build, this follows the usual EFI kernel-image handoff
 * model: clean the actual image/data ranges by VA, rather than issuing global
 * DC CISW operations or reading cache geometry ID registers potentially trapped
 * by an EL2 monitor. Preserve all range metadata in registers before cleaning.
 * The loader has verified identity mappings for each range. */
ASM_FUNC(B2LXnuHandoff)
    mov x19, x0
    mov x20, x1
    ldp x21, x22, [x2, #0]
    ldp x23, x24, [x2, #16]
    ldp x25, x26, [x2, #32]
    ldp x27, x28, [x2, #48]
    msr daifset, #0xf
    dsb sy
    B2L_STAGE 2, 0xff00
    B2L_STAGE 3, 0xffff
    mov x0, x23
    mov x1, x24
    bl .Lclean_range
    mov x0, x25
    mov x1, x26
    bl .Lclean_range
    mov x0, x27
    mov x1, x28
    bl .Lclean_range
    B2L_STAGE 3, 0xff00
    mrs x1, sctlr_el1
    bic x1, x1, #1
    bic x1, x1, #4
    bic x1, x1, #0x1000
    dsb sy
    msr sctlr_el1, x1
    isb
    B2L_STAGE 4, 0xff00
    tlbi vmalle1
    dsb sy
    isb
    B2L_STAGE 5, 0xff00
    mov x0, x20
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    br x19
/* Clean image/data to PoC, then invalidate its I-cache lines to PoU. CTR_EL0
 * describes line sizes without CSSELR/CCSIDR or global cache-set operations.
 * x0 start, x1 exclusive end; x2..x8 and flags clobbered. No stack use. */
.Lclean_range:
    mov x8, x0
    mrs x2, ctr_el0
    ubfx x3, x2, #16, #4
    mov x4, #4
    lsl x3, x4, x3
    sub x5, x3, #1
    bic x0, x0, x5
.Ldc_line:
    dc cvac, x0
    add x0, x0, x3
    cmp x0, x1
    b.lo .Ldc_line
    dsb sy
    and x3, x2, #0xf
    lsl x3, x4, x3
    sub x5, x3, #1
    bic x0, x8, x5
.Lic_line:
    ic ivau, x0
    add x0, x0, x3
    cmp x0, x1
    b.lo .Lic_line
    dsb sy
    isb
    ret
