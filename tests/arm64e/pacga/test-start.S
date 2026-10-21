/* SPDX-License-Identifier: BSD-2-Clause */
.text
.globl _start
_start:
    ldr x0, =0x41000000
    mov sp, x0
    mov x18, #0x1234
    bl main
    mov x1, #0x1234
    cmp x18, x1
    b.eq 1f
    mov x0, #5
1:  sub sp, sp, #16
    mov x1, #0x26
    movk x1, #0x2, lsl #16
    stp x1, x0, [sp]
    mov x1, sp
    mov x0, #0x20
    hlt #0xf000
    b .
