/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../uefi/XnuLoader/Arena.h"
#include "b2l_secondary.h"
static void desc(unsigned char *p,uint32_t type,uint64_t base,uint64_t bytes){
 uint64_t pages=bytes/4096;memset(p,0,48);memcpy(p,&type,4);memcpy(p+8,&base,8);memcpy(p+24,&pages,8);
}
int main(int argc,char **argv){
 assert(argc==2);unsigned char map[48*4];uint64_t pa=123;
 desc(map,0,0x80000000,0x4b400000); /* Existing lower guards/firmware remain reserved. */
 desc(map+48,7,0xcefc0000,0x40000); /* A real free part of the existing EFI heap. */
 desc(map+96,0,0xcf000000,0x31000000);
 desc(map+144,7,0x900000000ULL,0xfd800000);
 assert(!b2l_choose_secondary(map,sizeof(map),48,&pa) && pa==0xceffc000);
 assert(!b2l_validate_secondary(map,sizeof(map),48,pa,7));
 assert(b2l_validate_secondary(map,sizeof(map),48,pa,1));
 desc(map+48,1,pa,B2L_SECONDARY_BYTES);
 assert(!b2l_validate_secondary(map,sizeof(map),48,pa,1));
 /* The final EFI map must reject runtime bits, overlapping aliases and holes. */
 map[48+39]=0x80;assert(b2l_validate_secondary(map,sizeof(map),48,pa,1));map[48+39]=0;
 desc(map+144,1,pa,B2L_SECONDARY_BYTES);assert(b2l_validate_secondary(map,sizeof(map),48,pa,1));
 desc(map+144,7,0x900000000ULL,0x40000000);
 desc(map+48,1,pa+4096,B2L_SECONDARY_BYTES-4096);assert(b2l_validate_secondary(map,sizeof(map),48,pa,1));
 assert(b2l_choose_secondary(map,sizeof(map)-1,48,&pa)==-1);
 assert(b2l_choose_secondary(map,sizeof(map),39,&pa)==-1);
 const uint64_t protected[]={0x87000000,0x871fc000,0xc9000000,0xc9400000,0xca000000,0xcb3fc000,0xcf000000,0xcf040000};
 for(unsigned i=0;i<sizeof(protected)/sizeof(protected[0]);++i){
  assert(!b2l_secondary_address(protected[i]));
  desc(map+48,7,protected[i],B2L_SECONDARY_BYTES);uint64_t unchanged=123;
  assert(b2l_choose_secondary(map,sizeof(map),48,&unchanged)!=0 && unchanged==123);
 }
 assert(!b2l_secondary_address(0x7fffc000));assert(!b2l_secondary_address(0x100000000ULL));
 assert(!b2l_secondary_address(0xceffc001));assert(b2l_secondary_address(0xffffc000));
 memset(map,0,sizeof(map));desc(map,7,UINT64_MAX-4095,8192);
 assert(b2l_choose_secondary(map,48,48,&pa)==-1);
 /* Validate the actual cross-assembled 152-byte template and high target. */
 unsigned long long words[19];FILE *f=fopen(argv[1],"rb");assert(f);
 assert(fread(words,1,152,f)==152 && fgetc(f)==EOF);fclose(f);
 assert(b2l_secondary_template(words));assert(!words[5] && !words[6]);
 words[5]=0x900506000ULL;assert(words[5]==0x900506000ULL && b2l_secondary_template(words));
 words[6]=1;assert(b2l_secondary_template(words));
 for(unsigned i=0;i<16;++i)if(i!=5 && i!=6)for(unsigned bit=0;bit<64;++bit){
  words[i]^=1ULL<<bit;assert(!b2l_secondary_template(words));words[i]^=1ULL<<bit;
 }
 puts("Low secondary allocation: reservations, ownership, aliases, overflow and actual 64-bit ARM64 template passed");
}
