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
    br x16
    .quad 0x3130504d534c3242
.Ltarget:
    .quad 0
.Lentered:
    .quad 0
    .quad 1
B2LSecondaryTemplateEnd:
