/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include "../xnu-overlay/pexpert/arm/b2l_gic_target.h"
int main(void)
{
    for (unsigned a=0;a<1024;++a) {
        unsigned valid=a&&a<=255&&!(a&(a-1));
        assert((unsigned)b2l_gic_valid_target(a,0)==valid);
        assert(b2l_gic_sgir(a)==(valid ? (a<<16)|1U : 0));
        if(valid) {
            assert(!b2l_gic_valid_target(a,a));
            for(unsigned b=1;b<=128;b<<=1)
                assert((unsigned)b2l_gic_valid_target(a,b)==(a!=b));
        }
    }
    /* Nonidentity physical target mapping: logical CPU1 can have bit5. */
    assert(b2l_gic_sgir(1U<<5)==0x00200001U);
    assert(!(b2l_gic_sgir(1)&0xff000000U)); /* explicit target list, not broadcast */
    puts("GICv2 discovered-mask, duplicate, and SGI encoding tests passed");
}
