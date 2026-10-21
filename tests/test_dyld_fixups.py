#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile actual dyld pager dispatch/auth fixups against synthetic native pages."""
import pathlib,re,subprocess,sys,tempfile
x=pathlib.Path(sys.argv[1]);s=(x/'osfmk/vm/vm_dyld_pager.c').read_text()
def extract(name,signature=None):
 m=re.search(r'(?m)^'+re.escape(signature or name+'('),s);assert m,name
 begin=s.rfind('static ',0,m.start());brace=s.index('{',m.end());depth=1
 token=re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|[{}]',re.S)
 for t in token.finditer(s,brace+1):
  if t.group()=='{':depth+=1
  elif t.group()=='}':
   depth-=1
   if not depth:return s[begin:t.end()]
 raise AssertionError(name)
m=(x/'osfmk/mach/dyld_pager.h').read_text();m=m[m.index('struct mwl_info_hdr {'):];m=m[:m.index('};')+2]
functions=[extract('_delta_ptr_within_page_nospec'),extract('signPointer','signPointer(uint64_t'),extract('fixupPageAuth64'),extract('fixupCachePageAuth64'),extract('fixup_page')]
h=r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ptrauth.h>
#include <mach-o/fixup-chains.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define PAGE_MASK 4095
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define ktriage_record(...) ((void)0)
#define panic(...) abort()
#define SPECULATION_GUARD_ZEROING_XXX(out,valid,value,a,b,cc) do {valid=((a)==(b));out=valid?(value):0;}while(0)
typedef int kern_return_t;
typedef uintptr_t vm_offset_t;
static int panic_on_dyld_issue;
static bool enabled=true;
static bool b2l_arm64e_compat_enabled(void){return enabled;}
struct dyld_pager {void *dyld_link_info; uint32_t dyld_link_info_size;};
typedef struct dyld_pager *dyld_pager_t;
static int fixupPage64(uint64_t u,vm_offset_t c,void*l,struct dyld_chained_starts_in_segment*s,uint32_t i,bool b){(void)u;(void)c;(void)l;(void)s;(void)i;(void)b;abort();}
static int fixupPage32(uint64_t u,vm_offset_t c,void*l,uint32_t z,struct dyld_chained_starts_in_segment*s,uint32_t i){(void)u;(void)c;(void)l;(void)z;(void)s;(void)i;abort();}
"""+m+'\n'+'\n'.join(functions)+r"""
static uint8_t *blob,*page;
static struct mwl_info_hdr *hdr;
static struct dyld_chained_starts_in_segment *seg;
static struct dyld_pager pager;
static uint64_t base=UINT64_C(0x100004000);
static uint64_t *binds;
static void reset(unsigned format){
 memset(blob,0,1024*1024);memset(page,0,PAGE_SIZE);
 hdr=(void*)blob;hdr->mwli_page_size=PAGE_SIZE;hdr->mwli_pointer_format=format;
 hdr->mwli_image_address=UINT64_C(0x100000000);hdr->mwli_slide=0x4000;
 hdr->mwli_binds_offset=4096;hdr->mwli_binds_count=0x10002;binds=(void*)(blob+4096);
 binds[1]=0x180000000;binds[0x10001]=0x180001000;
 hdr->mwli_chains_offset=64;
 struct dyld_chained_starts_in_image *starts=(void*)(blob+64);
 starts->seg_count=1;starts->seg_info_offset[0]=32;
 seg=(void*)(blob+96);seg->size=32;seg->page_count=2;seg->page_size=PAGE_SIZE;
 seg->segment_offset=0x4000;seg->pointer_format=format;seg->page_start[0]=0;
 seg->page_start[1]=0;pager.dyld_link_info=blob;pager.dyld_link_info_size=1024*1024;
}
static uint64_t at(unsigned n){uint64_t v;memcpy(&v,page+n*8,8);return v;}
static void put(unsigned n,uint64_t v){memcpy(page+n*8,&v,8);}
int main(void){
 blob=calloc(1,1024*1024);page=aligned_alloc(PAGE_SIZE,PAGE_SIZE);assert(blob&&page);
 for(unsigned f=1;f<=12;f+=(f==1?8:3)){
  reset(f);
  put(0,UINT64_C(0x8000000000000248)|(UINT64_C(1)<<51));
  put(1,UINT64_C(0xc000000000000001)|(UINT64_C(2)<<49)|(UINT64_C(1)<<51));
  put(2,UINT64_C(0x4000000000000001)|((UINT64_C(0x80000)-17)<<32)|(UINT64_C(1)<<51));
  put(3,UINT64_C(0xa5)<<43|0x600);
  assert(fixup_page((uintptr_t)page,base,&pager)==0);
  assert(at(0)==hdr->mwli_image_address+0x248&&at(1)==binds[1]&&at(2)==binds[1]-17);
  assert(at(3)==(UINT64_C(0xa500000000000600)+(f==1?hdr->mwli_slide:hdr->mwli_image_address)));
 }
 reset(12);put(0,UINT64_C(0xc000000000010001));
 assert(!fixup_page((uintptr_t)page,base,&pager)&&at(0)==binds[0x10001]);
 reset(12);put(0,UINT64_C(0xc000000000010002));assert(fixup_page((uintptr_t)page,base,&pager)!=0);
 reset(12);put(0,UINT64_C(0x8002000000000001));assert(fixup_page((uintptr_t)page,base,&pager)!=0);
 reset(12);seg->page_start[0]=3;assert(fixup_page((uintptr_t)page,base,&pager)!=0);
 reset(12);seg->page_start[0]=PAGE_SIZE;assert(fixup_page((uintptr_t)page,base,&pager)!=0);
 reset(12);put(0,UINT64_C(512)<<51);assert(fixup_page((uintptr_t)page,base,&pager)!=0);
 reset(12);seg->page_size=16384;assert(fixup_page((uintptr_t)page,base,&pager)!=0);
 reset(12);hdr->mwli_page_size=16384;assert(fixup_page((uintptr_t)page,base,&pager)!=0);
 reset(12);seg->page_start[0]=DYLD_CHAINED_PTR_START_NONE;assert(!fixup_page((uintptr_t)page,base,&pager));
 reset(12);put(0,UINT64_C(0x8000000000000500));assert(!fixup_page((uintptr_t)page,base+4096,&pager)&&at(0)==hdr->mwli_image_address+0x500);
 reset(12);put(0,UINT64_C(0x8000000000000500));enabled=false;assert(fixup_page((uintptr_t)page,base,&pager)!=0);enabled=true;
 reset(13);put(0,UINT64_C(0x8000000000000250)|(UINT64_C(1)<<52));put(1,0x650);
 assert(!fixup_page((uintptr_t)page,base,&pager)&&at(0)==hdr->mwli_image_address+0x250&&at(1)==hdr->mwli_image_address+0x650);
 reset(11);assert(fixup_page((uintptr_t)page,base,&pager)!=0);
 free(blob);free(page);puts("Actual dyld pager: formats1/9/12/13, raw bind/rebase, 24-bit ordinal, addends, page bounds and disabled-mode gates passed");
}
"""
with tempfile.TemporaryDirectory(prefix='b2l-dyld-fixup-test-') as td:
 p=pathlib.Path(td);(p/'test.c').write_text(h)
 (p/'mach-o').mkdir();(p/'mach-o/fixup-chains.h').write_bytes((x/'EXTERNAL_HEADERS/mach-o/fixup-chains.h').read_bytes())
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run(['xcrun','clang','-isysroot',sdk,'-I'+str(p),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-g','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
