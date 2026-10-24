/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_CPU_AFFINITY_H
#define B2L_CPU_AFFINITY_H
#define B2L_A55_PART 0xd05
#define B2L_A75_PART 0xd0a
#define B2L_M4_PART  0x003
#if !defined(ASSEMBLER) && !defined(__ASSEMBLER__)
#include <stdint.h>
/* Measured EL1 identities: ARM raw0x000..0x500 uses Aff1 as core;
 * M4 raw0x100 (CPU6) uses architectural Aff1 cluster/Aff0 core. Its
 * second-core trial expects0x101. MIDR disambiguates raw0x100, shared
 * by A55 CPU1 and M4 CPU6. Do not infer identity from affinity alone. */
static inline int b2l_cpu_from_identity(uint64_t mpidr,uint64_t midr)
{
    uint64_t hwid=mpidr & UINT64_C(0xff00ffffff);
    if(hwid & ~UINT64_C(0xffff))return -1;
    unsigned implementer=(unsigned)((midr>>24)&255),part=(unsigned)((midr>>4)&4095);
    if(implementer==0x53 && part==B2L_M4_PART)
        return hwid>=0x100 && hwid<=0x101 ? 6+(int)(hwid-0x100) : -1;
    if(implementer!=0x41 || (hwid&255))return -1;
    unsigned core=(unsigned)(hwid>>8);
    return core<6 && part==(core<4?B2L_A55_PART:B2L_A75_PART) ? (int)core : -1;
}
static inline int b2l_cpu_firmware_id(unsigned cpu)
{
    return cpu<6?(int)cpu:cpu<8?(int)(0x100+cpu-6):-1;
}
static inline int b2l_cpu_midr_matches(unsigned cpu,uint64_t midr)
{
    unsigned implementer=(unsigned)((midr>>24)&255),part=(unsigned)((midr>>4)&4095);
    if(cpu<6)return implementer==0x41 && part==(cpu<4?B2L_A55_PART:B2L_A75_PART);
    return cpu<8 && implementer==0x53 && part==B2L_M4_PART;
}
#endif
#endif
