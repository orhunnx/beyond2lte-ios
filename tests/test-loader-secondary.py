#!/usr/bin/env python3
"""Run the real EFI AllocateSecondary body with mocked firmware services.
Only physical dereferences and cache-maintenance assembly are replaced for host
execution. Real assembly bytes and real EFI map selection/validation are used.
"""
from pathlib import Path
import subprocess, sys, tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'uefi/XnuLoader/XnuLoader.c').read_text()
s=s[s.index('static EFI_STATUS AllocateSecondary('):s.index('\nEFI_STATUS EFIAPI UefiMain(')]
a=s.index('        /* No global set/way cache operations or EL2 writes. */')
b=s.index('        *Page=Actual;return EFI_SUCCESS;',a)
s=s[:a]+s[b:]
s=s.replace('(VOID*)(UINTN)Actual','test_page').replace('(const volatile unsigned long long*)(UINTN)Actual','(const volatile unsigned long long*)test_page')
h=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "Arena.h"
#include "b2l_secondary.h"
typedef void VOID; typedef uintptr_t UINTN; typedef uint32_t UINT32;
typedef uint64_t UINT64, EFI_PHYSICAL_ADDRESS; typedef int EFI_STATUS;
#define B2L_MAP_SPACE 1024
#define EFI_SUCCESS 0
#define EFI_OUT_OF_RESOURCES 1
#define EFI_COMPROMISED_DATA 2
#define EFI_UNSUPPORTED 3
#define EFI_ERROR(x) ((x)!=0)
#define AllocateAddress 2
#define EfiLoaderCode 1
#define ZeroMem(p,n) memset(p,0,n)
#define CopyMem(p,s,n) memcpy(p,s,n)
static unsigned char test_page[B2L_SECONDARY_BYTES] __attribute__((aligned(16)));
static unsigned char template[152];
static const unsigned char *B2LSecondaryTemplate=template,*B2LSecondaryTemplateEnd;
static int allocated,fail_alloc,bad_map,identity_ok=1,alloc_calls,free_calls;
static int IdentityRange(uint64_t a,uint64_t n){return identity_ok && a==0xceffc000 && n==B2L_SECONDARY_BYTES;}
static EFI_STATUS getmap(UINTN *size,void *map,UINTN *key,UINTN *stride,UINT32 *version){
 unsigned char *m=map;uint32_t type=allocated&&!bad_map?1:7;
 uint64_t base=0xceffc000,pages=4;
 assert(*size>=48);memset(m,0,48);memcpy(m,&type,4);memcpy(m+8,&base,8);memcpy(m+24,&pages,8);
 *size=48;*key=1;*stride=48;*version=1;return 0;
}
static EFI_STATUS allocpages(int how,int type,UINTN count,EFI_PHYSICAL_ADDRESS *actual){
 assert(how==AllocateAddress && type==EfiLoaderCode && count==4 && *actual==0xceffc000);
 ++alloc_calls;if(fail_alloc)return EFI_OUT_OF_RESOURCES;allocated=1;return 0;
}
static EFI_STATUS freepages(EFI_PHYSICAL_ADDRESS actual,UINTN count){
 assert(actual==0xceffc000 && count==4 && allocated);allocated=0;++free_calls;return 0;
}
static struct {EFI_STATUS (*GetMemoryMap)(UINTN*,void*,UINTN*,UINTN*,UINT32*);
 EFI_STATUS (*AllocatePages)(int,int,UINTN,EFI_PHYSICAL_ADDRESS*);
 EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS,UINTN);} services={getmap,allocpages,freepages},*gBS=&services;
'''
t=r'''
static void reset(void){allocated=fail_alloc=bad_map=alloc_calls=free_calls=0;identity_ok=1;memset(test_page,0xa5,sizeof(test_page));B2LSecondaryTemplateEnd=template+152;}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(template,1,152,f)==152 && fgetc(f)==EOF);fclose(f);
 unsigned char map[B2L_MAP_SPACE];EFI_PHYSICAL_ADDRESS page=123;
 reset();assert(AllocateSecondary(map,&page)==EFI_SUCCESS && page==0xceffc000 && alloc_calls==1 && free_calls==0);
 assert(!memcmp(test_page,template,152));assert(b2l_secondary_template((void*)test_page));
 for(unsigned i=152;i<sizeof(test_page);++i)assert(!test_page[i]);
 reset();B2LSecondaryTemplateEnd=template+64;assert(AllocateSecondary(map,&page)==EFI_UNSUPPORTED && free_calls==1);
 reset();template[80]^=1;assert(AllocateSecondary(map,&page)==EFI_COMPROMISED_DATA && free_calls==1);template[80]^=1;
 reset();fail_alloc=1;assert(AllocateSecondary(map,&page)==EFI_OUT_OF_RESOURCES && alloc_calls==4 && !free_calls);
 reset();bad_map=1;assert(AllocateSecondary(map,&page)==EFI_COMPROMISED_DATA && free_calls==1);
 reset();identity_ok=0;assert(AllocateSecondary(map,&page)==EFI_UNSUPPORTED && free_calls==1);
 puts("Actual EFI AllocateSecondary: full152-byte copy, ABI rejection, corruption, retries, ownership and identity PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-loader-secondary-') as td:
 p=Path(td);(p/'test.c').write_text(h+s+t)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(r/'uefi/XnuLoader'),'-I'+str(r/'common/smp'),str(p/'test.c'),str(r/'uefi/XnuLoader/Arena.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),sys.argv[1]],check=True)
