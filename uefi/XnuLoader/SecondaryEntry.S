/* SPDX-License-Identifier: BSD-2-Clause */
.text
.p2align 3
.globl B2LSecondaryTemplate
.globl B2LSecondaryTemplateEnd
B2LSecondaryTemplate:
    msr daifset, #0xf
    adr x17, .Lentered
    mov x16, #1
    str x16, [x17]
    dc civac, x17
    dsb sy
    ldr x16, .Ltarget
    b .Lcapture
    .quad 0x3130504d534c3242
.Ltarget:
    .quad 0
.Lentered:
    .quad 0
    .quad 2
/* Capture before the high physical branch, with no stack, calls or MMU. */
.Lcapture:
    adr x17, .Lidentity
    mrs x15, CurrentEL
    str x15, [x17]
    mrs x15, MPIDR_EL1
    str x15, [x17, #8]
    mrs x15, MIDR_EL1
    str x15, [x17, #16]
    dc civac, x17
    dsb sy
    adr x17, .Lentered
    mov x15, #2
    str x15, [x17]
    dc civac, x17
    dsb sy
    br x16
.balign 128
.Lidentity:
    .quad 0, 0, 0
B2LSecondaryTemplateEnd:
