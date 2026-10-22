/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../uefi/XnuLoader/Arena.h"
static void desc(unsigned char *p,uint32_t type,uint64_t base,uint64_t size) {
    uint64_t pages=size/4096;memset(p,0,48);memcpy(p,&type,4);memcpy(p+8,&base,8);memcpy(p+24,&pages,8);
}
int main(void){
    unsigned char map[48*6];uint64_t base=123,size=456;
    desc(map,0,0x80000000,0x80000000); /* All low-bank reservations unchanged. */
    desc(map+48,0,0x880000000ULL,0x80000000); /* sec_rdx bank remains guarded. */
    desc(map+96,7,0x900000000ULL,0xfd800000); /* Only verified contiguous span. */
    desc(map+144,0,0x9fd800000ULL,0x2800000); /* Actual EL2 early memory. */
    desc(map+192,7,0xA00000000ULL,0x200000000ULL); /* Outside actual board. */
    desc(map+240,7,0xcb400000,60ULL*1024*1024);
    assert(!b2l_choose_arena(map,sizeof(map),48,&base,&size));
    assert(base==B2L_RAM_START && size==4024ULL*1024*1024);
    assert(base+size+B2L_EFI_HEADROOM==B2L_RAM_END);
    assert(b2l_validate_arena(map,sizeof(map),48,base,size,2));
    desc(map+96,2,base,size);
    assert(!b2l_validate_arena(map,sizeof(map),48,base,size,2));
    /* Split allocation descriptors in unsorted order are legitimate. */
    desc(map+96,2,base+size/2,size/2);
    desc(map+240,2,base,size/2);
    assert(!b2l_validate_arena(map,sizeof(map),48,base,size,2));
    desc(map+240,2,base,size/2-4096); /* Hole. */
    assert(b2l_validate_arena(map,sizeof(map),48,base,size,2));
    desc(map+240,2,base,size/2+4096); /* Overlap. */
    assert(b2l_validate_arena(map,sizeof(map),48,base,size,2));
    desc(map+240,7,base,size/2); /* Ownership changed. */
    assert(b2l_validate_arena(map,sizeof(map),48,base,size,2));
    desc(map+240,2,base,size/2);
    map[240+39]=0x80; /* EFI_MEMORY_RUNTIME. */
    assert(b2l_validate_arena(map,sizeof(map),48,base,size,2));
    desc(map+240,2,base,size/2);
    assert(b2l_validate_arena(map,sizeof(map),48,base,size+0x4000000,2));
    desc(map+96,7,0x900001000ULL,0xfd7ff000);
    desc(map+240,7,0xcb400000,60ULL*1024*1024);
    assert(!b2l_choose_arena(map,sizeof(map),48,&base,&size));
    assert(base==0x900200000ULL && size==4022ULL*1024*1024);
    desc(map+96,7,0x900000000ULL,B2L_MIN_ARENA+B2L_EFI_HEADROOM);
    assert(!b2l_choose_arena(map,sizeof(map),48,&base,&size) && size==B2L_MIN_ARENA);
    desc(map+96,7,0x900000000ULL,B2L_MIN_ARENA);
    base=123;size=456;
    assert(b2l_choose_arena(map,sizeof(map),48,&base,&size)==-2);
    assert(base==123 && size==456);
    desc(map+96,7,UINT64_MAX-4095,8192);
    assert(b2l_choose_arena(map,sizeof(map),48,&base,&size)==-1);
    assert(b2l_choose_arena(map,sizeof(map)-1,48,&base,&size)==-1);
    assert(b2l_choose_arena(map,sizeof(map),39,&base,&size)==-1);
    puts("EFI arena: actual EL2 reserve, 4024MiB limit, headroom, final ownership and holes passed");
}
