/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "b2l_cpu_affinity.h"
int main(void) {
    for (unsigned hwid=0; hwid<65536; ++hwid) {
        int expected = !(hwid&255) && (hwid>>8)<6 ? (int)(hwid>>8) : -1;
        assert(b2l_cpu_from_mpidr(hwid)==expected);
        assert(b2l_cpu_from_mpidr(UINT64_C(0x81000000)|hwid)==expected);
    }
    for (unsigned cpu=0;cpu<6;++cpu) {
        uint64_t mpidr=(uint64_t)cpu<<8;
        assert(b2l_cpu_from_mpidr(mpidr|(1ULL<<16))==-1);
        assert(b2l_cpu_from_mpidr(mpidr|(1ULL<<32))==-1);
        uint64_t midr=cpu<4?0x411fd050:0x412fd0a1;
        assert(b2l_cpu_midr_matches(cpu,midr));
        assert(!b2l_cpu_midr_matches(cpu,0x531f0030));
        assert(!b2l_cpu_midr_matches(cpu,cpu<4?0x412fd0a1:0x411fd050));
    }
    puts("EL1 affinity translation:65536 affinities, flags, upper affinity and A55/A75/M4 identity gates passed");
}
