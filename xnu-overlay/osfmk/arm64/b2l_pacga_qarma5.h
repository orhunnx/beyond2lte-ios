/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_PACGA_QARMA5_H
#define B2L_PACGA_QARMA5_H
#include <stdint.h>
/* APGAKeyLo_EL1 supplies key_lo; APGAKeyHi_EL1 supplies key_hi.
 * QARMA5 PACGA result: cipher bits 63:32, with bits 31:0 cleared.
 * Does not read system registers, implement instruction traps, or own keys. */
uint64_t b2l_pacga_qarma5(uint64_t data, uint64_t modifier,
                         uint64_t key_lo, uint64_t key_hi);
#if defined(__aarch64__)
/* Scalar AArch64 fast path with identical result and AAPCS64 calling convention. */
uint64_t b2l_pacga_qarma5_asm(uint64_t data, uint64_t modifier,
                             uint64_t key_lo, uint64_t key_hi);
#endif
#endif
