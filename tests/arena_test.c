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
    unsigned char map[48*4];uint64_t base=0,size=0;
    desc(map,0,0x80000000,0x80000000); /* Reserved lower bank. */
    desc(map+48,11,0x10000000,0x800000000); /* MMIO must never win. */
    desc(map+96,7,0xcb400000,60ULL*1024*1024);
    desc(map+144,7,0x900000000ULL,2ULL*1024*1024*1024);
    assert(!b2l_choose_arena(map,sizeof(map),48,&base,&size));
    assert(base==0x900000000ULL && size==2ULL*1024*1024*1024-B2L_EFI_HEADROOM);
    desc(map+144,7,0x900001000ULL,4ULL*1024*1024*1024);
    assert(!b2l_choose_arena(map,sizeof(map),48,&base,&size));
    assert(base==0x900200000ULL && size==B2L_MAX_ARENA);
    desc(map+144,7,0x900000000ULL,B2L_MIN_ARENA+B2L_EFI_HEADROOM);
    assert(!b2l_choose_arena(map,sizeof(map),48,&base,&size) && size==B2L_MIN_ARENA);
    desc(map+144,7,0x900000000ULL,B2L_MIN_ARENA);
    assert(b2l_choose_arena(map,sizeof(map),48,&base,&size)==-2);
    desc(map+144,7,UINT64_MAX-4095,8192);
    assert(b2l_choose_arena(map,sizeof(map),48,&base,&size)==-2);
    assert(b2l_choose_arena(map,sizeof(map)-1,48,&base,&size)==-1);
    assert(b2l_choose_arena(map,sizeof(map),39,&base,&size)==-1);
    puts("EFI arena bounds, reserved regions, alignment and size policy tests passed");
}
