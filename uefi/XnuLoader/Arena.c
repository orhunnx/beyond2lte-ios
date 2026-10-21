/* SPDX-License-Identifier: BSD-2-Clause */
#include "Arena.h"
static b2l_u32 get32(const b2l_u8 *p)
{return (b2l_u32)p[0]|(b2l_u32)p[1]<<8|(b2l_u32)p[2]<<16|(b2l_u32)p[3]<<24;}
static b2l_u64 get64(const b2l_u8 *p){return get32(p)|(b2l_u64)get32(p+4)<<32;}
int b2l_choose_arena(const void *map, b2l_size map_size, b2l_size stride,
                     b2l_u64 *start, b2l_u64 *size)
{
    const b2l_u8 *p=map;
    b2l_u64 best_start=0,best_size=0,max=~(b2l_u64)0;
    if(!map || !start || !size || stride<40 || map_size%stride) return -1;
    for(b2l_size off=0;off<map_size;off+=stride){
        b2l_u64 base=get64(p+off+8),pages=get64(p+off+24),begin,end,span;
        if(get32(p+off)!=7 || pages>max/4096 || base>max-pages*4096 ||
           base>max-(B2L_ALIGNMENT-1)) continue;
        begin=(base+B2L_ALIGNMENT-1)&~(B2L_ALIGNMENT-1);
        end=(base+pages*4096)&~(B2L_ALIGNMENT-1);
        if(end<=begin || end-begin<=B2L_EFI_HEADROOM) continue;
        span=(end-begin-B2L_EFI_HEADROOM)&~(B2L_ALIGNMENT-1);
        if(span>B2L_MAX_ARENA) span=B2L_MAX_ARENA;
        if(span>=B2L_MIN_ARENA && span>best_size){best_start=begin;best_size=span;}
    }
    if(!best_size) return -2;
    *start=best_start;*size=best_size;return 0;
}
