/* SPDX-License-Identifier: BSD-2-Clause */
#include "Arena.h"
static b2l_u32 get32(const b2l_u8 *p)
{return (b2l_u32)p[0]|(b2l_u32)p[1]<<8|(b2l_u32)p[2]<<16|(b2l_u32)p[3]<<24;}
static b2l_u64 get64(const b2l_u8 *p){return get32(p)|(b2l_u64)get32(p+4)<<32;}
static int descriptor(const b2l_u8 *p,b2l_u64 *base,b2l_u64 *end)
{
    b2l_u64 pages=get64(p+24),max=~(b2l_u64)0;
    *base=get64(p+8);
    if((*base&4095) || !pages || pages>max/4096 || *base>max-pages*4096) return -1;
    *end=*base+pages*4096; return 0;
}
int b2l_validate_arena(const void *map,b2l_size map_size,b2l_size stride,
                       b2l_u64 start,b2l_u64 size,b2l_u32 type)
{
    const b2l_u8 *p=map;
    b2l_u64 end,cursor=start;
    if(!p || stride<40 || map_size%stride || !size ||
       (start&(B2L_ALIGNMENT-1)) || (size&(B2L_ALIGNMENT-1)) ||
       start<B2L_RAM_START || start>=B2L_RAM_END || size>B2L_RAM_END-start || B2L_RAM_END-start-size<B2L_EFI_HEADROOM ||
       size>B2L_MAX_ARENA || size<B2L_MIN_ARENA) return -1;
    end=start+size;
    /* EFI does not require descriptor sorting. Require unique complete coverage;
     * reject overlapping aliases, holes, runtime pages, or changed ownership. */
    while(cursor<end){
        b2l_u64 next=cursor;
        for(b2l_size off=0;off<map_size;off+=stride){
            b2l_u64 base,limit;
            if(descriptor(p+off,&base,&limit)) return -1;
            if(base>=end || limit<=start) continue;
            if(get32(p+off)!=type || (get64(p+off+32)&(1ULL<<63))) return -1;
            if(base<=cursor && limit>cursor){
                if(next!=cursor || (cursor!=start && base<cursor)) return -1;
                next=limit<end?limit:end;
            } else if(base<cursor && limit>start) {
                /* Already consumed descriptors are expected. */
            }
        }
        if(next==cursor) return -1;
        /* Any second descriptor overlapping this newly covered interval is bad. */
        unsigned overlaps=0;
        for(b2l_size off=0;off<map_size;off+=stride){
            b2l_u64 base,limit;
            if(descriptor(p+off,&base,&limit)) return -1;
            if(base<next && limit>cursor) ++overlaps;
        }
        if(overlaps!=1) return -1;
        cursor=next;
    }
    return 0;
}
int b2l_choose_arena(const void *map,b2l_size map_size,b2l_size stride,
                     b2l_u64 *start,b2l_u64 *size)
{
    const b2l_u8 *p=map;
    b2l_u64 best_start=0,best_size=0;
    if(!map || !start || !size || stride<40 || map_size%stride) return -1;
    for(b2l_size off=0;off<map_size;off+=stride){
        b2l_u64 base,end,begin,span;
        if(descriptor(p+off,&base,&end)) return -1;
        if(get32(p+off)!=7 || base>=B2L_RAM_END || end<=B2L_RAM_START) continue;
        if(base<B2L_RAM_START) base=B2L_RAM_START;
        if(end>B2L_RAM_END) end=B2L_RAM_END;
        begin=(base+B2L_ALIGNMENT-1)&~(B2L_ALIGNMENT-1);
        end&=~(B2L_ALIGNMENT-1);
        if(end<=begin || end-begin<=B2L_EFI_HEADROOM) continue;
        span=(end-begin-B2L_EFI_HEADROOM)&~(B2L_ALIGNMENT-1);
        if(span>B2L_MAX_ARENA) span=B2L_MAX_ARENA;
        if(span>=B2L_MIN_ARENA && span>best_size){
            if(b2l_validate_arena(map,map_size,stride,begin,span,7)) return -1;
            best_start=begin;best_size=span;
        }
    }
    if(!best_size) return -2;
    *start=best_start;*size=best_size;return 0;
}
