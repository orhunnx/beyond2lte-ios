/* SPDX-License-Identifier: BSD-2-Clause */
#include <AsmMacroLib.h>
.text
/* AAPCS64: x0 entry PA, x1 boot_args PA. The loader has verified identity
 * mappings. No stack or data references occur after the final cache flush.
 * This code uses the architected cache geometry rather than an EDK II global
 * cache-maintenance function: ArmCacheMaintenanceLib intentionally asserts for
 * whole-cache operations on this firmware revision. */
ASM_FUNC(B2LXnuHandoff)
    mov x19, x0
    mov x20, x1
    msr daifset, #0xf
    dsb sy
    mrs x1, clidr_el1
    ubfx x2, x1, #24, #3       // Number of cache levels to point of coherency.
    cbz x2, 5f
    lsl x2, x2, #1
    mov x3, #0                // CSSELR_EL1 level selector: level * 2.
    mrs x13, id_aa64mmfr2_el1
    ubfx x13, x13, #20, #4    // FEAT_CCIDX selects the extended CCSIDR format.
1:
    add x4, x3, x3, lsr #1
    lsr x4, x1, x4
    and x4, x4, #7
    cmp x4, #2                // Skip absent or instruction-only cache levels.
    b.lo 4f
    msr csselr_el1, x3
    isb
    mrs x4, ccsidr_el1
    and x5, x4, #7
    add x5, x5, #4            // log2(cache line bytes).
    cbnz x13, 2f
    ubfx x6, x4, #3, #10      // Legacy associativity minus one.
    ubfx x8, x4, #13, #15     // Legacy sets minus one.
    b 3f
2:
    ubfx x6, x4, #3, #21
    ubfx x8, x4, #32, #24
3:
    clz w7, w6                // Architected set/way operand way-bit position.
    mov x9, x6
6:
    mov x10, x8
7:
    lsl x11, x9, x7
    orr x11, x11, x3
    lsl x12, x10, x5
    orr x11, x11, x12
    dc cisw, x11
    subs x10, x10, #1
    b.ge 7b
    subs x9, x9, #1
    b.ge 6b
    dsb sy
4:
    add x3, x3, #2
    cmp x3, x2
    b.lo 1b
5:
    dsb sy
    msr csselr_el1, xzr
    ic iallu
    dsb sy
    isb
    mrs x1, sctlr_el1
    bic x1, x1, #1            // M: MMU.
    bic x1, x1, #4            // C: data cache.
    bic x1, x1, #0x1000       // I: instruction cache.
    msr sctlr_el1, x1
    isb
    tlbi vmalle1
    dsb sy
    isb
    mov x0, x20
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    br x19
