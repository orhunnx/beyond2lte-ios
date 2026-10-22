/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_CPU_AFFINITY_H
#define B2L_CPU_AFFINITY_H
#define B2L_A55_PART 0xd05
#define B2L_A75_PART 0xd0a
#if !defined(ASSEMBLER) && !defined(__ASSEMBLER__)
#include <stdint.h>
/* This boot chain reports the core index in EL1 Aff1. Firmware PSCI/DT
 * targets remain0..5. Never normalize Aff0=1 Cheetah aliases into this set. */
static inline int b2l_cpu_from_mpidr(uint64_t mpidr)
{
    uint64_t hwid = mpidr & UINT64_C(0xff00ffffff);
    if (hwid & ~UINT64_C(0xff00)) return -1;
    unsigned cpu = (unsigned)(hwid >> 8);
    return cpu < 6 ? (int)cpu : -1;
}
static inline int b2l_cpu_midr_matches(unsigned cpu, uint64_t midr)
{
    return cpu < 6 && ((midr >> 24) & 255) == 0x41 &&
        ((midr >> 4) & 4095) == (cpu < 4 ? B2L_A55_PART : B2L_A75_PART);
}
#endif
#endif
