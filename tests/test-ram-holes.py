#!/usr/bin/env python3
"""Exercise the actual prepared firmware RAM constructor and hole subtractor."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
s=(R/'out/build-firmware-env/mu/Silicon/Samsung/SamsungPkg/Library/RamManagerLib/RamManager.c').read_text()
def function(name):
 a=s.index(name+' (');b=s.index('{',a);depth=1;e=b+1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return 'EFI_STATUS '+s[a:e]
code=r"""
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define OUT
#define IN
#define EFIAPI
#define VOID void
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX_UINT64 UINT64_MAX
#define EFI_SUCCESS 0
#define EFI_NOT_READY 6
#define EFI_NOT_FOUND 14
#define EFI_OUT_OF_RESOURCES 9
#define EFI_COMPROMISED_DATA 33
#define EFI_ERROR(s) ((s)!=0)
#define BASE_2GB 0x80000000ULL
#define BASE_4GB 0x100000000ULL
#define SIZE_1GB 0x40000000ULL
typedef uint64_t EFI_PHYSICAL_ADDRESS,UINT64;typedef unsigned UINTN,EFI_STATUS;typedef uint8_t UINT8;
typedef void *EFI_HANDLE;typedef void EFI_SYSTEM_TABLE;
typedef struct{UINT64 Address,Length;}EFI_MEMORY_RANGE;
#define AllocateZeroPool(n) calloc(1,(n))
#define FreePool(p) free(p)
#define CopyMem(d,s,n) memcpy(d,s,n)
#define ZeroMem(p,n) memset(p,0,n)
static EFI_STATUS B2lRamStatus=EFI_NOT_READY,QueryStatus[4];
static EFI_PHYSICAL_ADDRESS MemoryStart,MemoryEnd;
static EFI_MEMORY_RANGE HoleRange[5];
static EFI_STATUS GetMemorySpace(void){MemoryStart=BASE_2GB;MemoryEnd=0xa00000000ULL;return 0;}
static EFI_STATUS query(unsigned i,EFI_PHYSICAL_ADDRESS *b,UINT64 *n){*b=0x87000000+0x10000*i;*n=0x10000;return QueryStatus[i];}
#define GetSecureDramRange(b,n) query(0,b,n)
#define GetHarxRange(b,n) query(1,b,n)
#define GetDrmPluginRange(b,n) query(2,b,n)
#define GetSecurePgtblRange(b,n) query(3,b,n)
"""+'\n'.join(function(x) for x in ('GetUsableMemoryRanges','GetMemoryHoles','GetMemoryData'))+r"""
int main(void){
 EFI_MEMORY_RANGE *r=NULL;UINT8 n=0;
 assert(GetUsableMemoryRanges(&r,&n)==EFI_NOT_READY&&!r);
 for(unsigned i=0;i<4;i++)QueryStatus[i]=EFI_NOT_FOUND;
 assert(!GetMemoryData(NULL,NULL));assert(!GetUsableMemoryRanges(&r,&n));assert(n==2);assert(r[0].Address==BASE_2GB&&r[0].Length==BASE_2GB);assert(r[1].Address==0x880000000ULL);free(r);
 for(unsigned q=0;q<4;q++){
  QueryStatus[q]=EFI_NOT_READY;assert(GetMemoryData(NULL,NULL)==EFI_NOT_READY);r=NULL;
  assert(GetUsableMemoryRanges(&r,&n)==EFI_NOT_READY&&!r);QueryStatus[q]=EFI_NOT_FOUND;
 }
 assert(!GetMemoryData(NULL,NULL));MemoryStart=100;MemoryEnd=1000;
 HoleRange[0]=(EFI_MEMORY_RANGE){700,400};HoleRange[1]=(EFI_MEMORY_RANGE){200,200};HoleRange[2]=(EFI_MEMORY_RANGE){300,300};HoleRange[3]=(EFI_MEMORY_RANGE){10,100};HoleRange[4]=(EFI_MEMORY_RANGE){1200,10};
 assert(!GetUsableMemoryRanges(&r,&n));assert(n==2&&r[0].Address==110&&r[0].Length==90&&r[1].Address==600&&r[1].Length==100);free(r);
 HoleRange[0]=(EFI_MEMORY_RANGE){UINT64_MAX-4,10};assert(GetUsableMemoryRanges(&r,&n)==EFI_COMPROMISED_DATA);
 puts("Firmware live RAM holes: unordered/overlapping/clipped/overflow cases and ignored-constructor failures passed");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
