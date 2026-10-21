/* Baseline AArch64 candidate computation only. Never branches to guest PCs,
 * touches guest memory, uses x18/FP/SIMD, or executes PAC instructions. */
#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
.text
.p2align 2
.globl SYM(b2l_compat_fast_candidate)
#ifndef __APPLE__
.type SYM(b2l_compat_fast_candidate), %function
#endif
/* w0=instruction, x1=source context, x2=distinct output context.
 * Return1: hot instruction recognized and candidate produced. Return0: no write.
 * x[31] at0, sp248, pc256, cpsr264; sizeof context272. */
SYM(b2l_compat_fast_candidate):
    and w3, w0, #0xffffe000
    mov w4, #0xdac10000
    cmp w3, w4
    b.eq .Lidentity
    movz w4, #0xe3e0
    movk w4, #0xffff, lsl #16
    and w3, w0, w4
    movz w4, #0x23e0
    movk w4, #0xdac1, lsl #16
    cmp w3, w4
    b.eq .Lidentity
    and w3, w0, #0xffffffe0
    movz w4, #0x43e0
    movk w4, #0xdac1, lsl #16
    cmp w3, w4
    b.eq .Lidentity
    movz w4, #0x47e0
    movk w4, #0xdac1, lsl #16
    cmp w3, w4
    b.eq .Lidentity
    /* Exact hint aliases. These fixed words do not consume guest operands. */
.macro hint word
    movz w4, #(\word & 0xffff)
    movk w4, #(\word >> 16), lsl #16
    cmp w0, w4
    b.eq .Lidentity
.endm
    hint 0xd50320ff
    hint 0xd503211f
    hint 0xd503215f
    hint 0xd503219f
    hint 0xd50321df
    hint 0xd503231f
    hint 0xd503233f
    hint 0xd503235f
    hint 0xd503237f
    hint 0xd503239f
    hint 0xd50323bf
    hint 0xd50323df
    hint 0xd50323ff
    and w3, w0, #0xfffff800
    movz w4, #0x0800
    movk w4, #0xd71f, lsl #16
    cmp w3, w4
    b.eq .Lbranch
    movk w4, #0xd73f, lsl #16
    cmp w3, w4
    b.eq .Lbranch
    movz w4, #0xf81f
    movk w4, #0xffff, lsl #16
    and w3, w0, w4
    movz w4, #0x081f
    movk w4, #0xd61f, lsl #16
    cmp w3, w4
    b.eq .Lbranch
    movk w4, #0xd63f, lsl #16
    cmp w3, w4
    b.eq .Lbranch
    /* RETAA/RETAB differ only at key bit10. */
    movz w4, #0xfbff
    movk w4, #0xffff, lsl #16
    and w3, w0, w4
    movz w4, #0x0bff
    movk w4, #0xd65f, lsl #16
    cmp w3, w4
    b.eq .Lreturn
    mov w0, #0
    ret
.Lidentity:
    ldr x6, [x1, #256]
    add x6, x6, #4
    mov w7, #0
    b .Lcopy
.Lreturn:
    ldr x6, [x1, #240]           /* old x30 */
    mov w7, #0
    b .Lcopy
.Lbranch:
    ubfx w3, w0, #5, #5
    cmp w3, #31
    b.eq .Lzero_target
    ldr x6, [x1, x3, lsl #3]   /* snapshot before possible link write */
    b .Lbranch_link
.Lzero_target:
    mov x6, #0
.Lbranch_link:
    ubfx w7, w0, #21, #1
.Lcopy:
    mov x3, x1
    mov x4, x2
    mov w5, #17
.Lcopy_loop:
    ldp x8, x9, [x3], #16
    stp x8, x9, [x4], #16
    subs w5, w5, #1
    b.ne .Lcopy_loop
    str x6, [x2, #256]
    cbz w7, .Ldone
    ldr x8, [x1, #256]
    add x8, x8, #4
    str x8, [x2, #240]
.Ldone:
    mov w0, #1
    ret
#ifndef __APPLE__
.size SYM(b2l_compat_fast_candidate), .-SYM(b2l_compat_fast_candidate)
.section .note.GNU-stack,"",%progbits
#endif
