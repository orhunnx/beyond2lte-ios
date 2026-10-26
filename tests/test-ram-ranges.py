#!/usr/bin/env python3
"""Execute production EFI and native page-iterator code over discontiguous RAM."""
from pathlib import Path
import os,re,subprocess,tempfile,importlib.util,json
R=Path(__file__).resolve().parents[1];X=Path(os.environ.get('XNU_PATH',R/'out/build-env-0063/src/xnu'))
s=(X/'osfmk/arm/pmap/pmap.c').read_text();a=s.index('static unsigned b2l_free_region;');z=s.index('\n#else',a);iterator=s[a:z]
assert 'b2l_pai_to_pa(pai)' in s and 'vm_first_phys + (pmap_paddr_t)ptoa(pai)' not in s
for path in ('common/memory/b2l_ram_ranges.h','xnu-overlay/pexpert/pexpert/arm64/b2l_ram_ranges.h'):
 assert (R/path).read_bytes()==(R/'common/memory/b2l_ram_ranges.h').read_bytes()
assert '#define XNU_VM_HAS_LINEAR_PAGES_ARRAY   0' in (X/'osfmk/vm/vm_page.h').read_text()

avm=(X/'osfmk/arm64/arm_vm_init.c').read_text()
def native(name):
 a=avm.index(name+'(');b=avm.index('{',a);depth=1;e=b+1
 while depth:depth+=(avm[e]=='{')-(avm[e]=='}');e+=1
 return avm[a:e]
cache_support=r"""
typedef uint64_t vm_size_t,vm_map_address_t,vm_offset_t,pmap_paddr_t;
#define PTOV_TABLE_SIZE 3
#define PAGE_MASK (PAGE_SIZE-1)
#define __improbable(x) (x)
#define panic(...) abort()
static uint64_t gVirtBase,real_phys_size;
static struct{uint64_t pa,va,len;}ptov_table[3];
static unsigned icalls;static uint64_t iva[4],ilen[4];
void InvalidatePoU_IcacheRegion(vm_offset_t va,size_t n){iva[icalls]=va;ilen[icalls++]=n;}
"""+'vm_map_address_t '+native('phystokv_range')+'\nvoid '+native('b2l_invalidate_icache_phys')

program=r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "Arena.h"
typedef uint64_t ppnum_t;typedef int boolean_t;
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define TRUE 1
#define FALSE 0
#define PAGE_SIZE B2L_RAM_PAGE
#define atop(x) ((x)/PAGE_SIZE)
struct b2l_ram_range b2l_owned_ram[B2L_RAM_MAX_RANGES];unsigned b2l_owned_ram_count;
static unsigned avail_page_count;static int need_ram_ranges_init=1;
static uint64_t first_avail,gPhysBase;boolean_t pmap_next_page(ppnum_t *);
static int b2l_managed_pa(uint64_t pa){return b2l_ram_contains_range(b2l_owned_ram,b2l_owned_ram_count,pa,1);}
"""+cache_support+iterator+r"""
static void desc(unsigned char *p,unsigned t,uint64_t pa,uint64_t bytes){uint64_t pages=bytes/4096;memset(p,0,48);memcpy(p,&t,4);memcpy(p+8,&pa,8);memcpy(p+24,&pages,8);}
int main(void){
 unsigned char map[48*12];memset(map,0,sizeof(map));unsigned n=0;struct b2l_ram_range extras[128];
 /* Deliberately unsorted descriptors, a 4KiB unavailable page and a gap. */
 desc(map,7,0x88ad00000ULL,0x73000000ULL);
 desc(map+48,7,0xb4800000ULL,0x6300000);
 desc(map+96,0,0xbab00000ULL,0x5500000);
 desc(map+144,7,0x9fb800000ULL,0x2000000); /* required EFI headroom */
 desc(map+192,7,0xcb400000ULL,0x1000); /* too small for one VM page */
 desc(map+240,7,0xcb404000ULL,0x3bfc000);
 desc(map+288,2,0x900000000ULL,4024ULL<<20);
 desc(map+336,7,0xc9000000ULL,0x440000); /* must be rejected by allowlist */
 desc(map+384,7,0x87000000ULL,0x200000); /* EL2 */
 desc(map+432,7,0x980000000ULL,0x10000);map[432+39]=0x80; /* runtime */
 desc(map+480,7,0xca000000ULL,0x1400000); /* GOP */
 desc(map+528,7,0x96000000ULL,0x2200000); /* secure camera */
 assert(!b2l_collect_extra_ram(map,sizeof(map),48,0x900000000ULL,4024ULL<<20,extras,&n));assert(n==3);
 assert(extras[0].base==0xb4800000ULL&&extras[1].base==0xcb404000ULL&&extras[2].base==0x88ad00000ULL);
 for(unsigned i=0;i<n;i++)assert(b2l_ram_allowed(extras[i].base,extras[i].size));
 /* All candidate pages must be claimed as LoaderData before handoff. */
 assert(b2l_validate_owned_ram(map,sizeof(map),48,extras,n));
 desc(map,2,extras[2].base,extras[2].size);desc(map+48,2,extras[0].base,extras[0].size);desc(map+240,2,extras[1].base,extras[1].size);
 assert(!b2l_validate_owned_ram(map,sizeof(map),48,extras,n));
 desc(map+96,2,extras[0].base+16384,16384);assert(b2l_validate_owned_ram(map,sizeof(map),48,extras,n)); /* alias */
 desc(map+96,0,0xbab00000ULL,0x5500000);
 b2l_owned_ram_count=4;b2l_owned_ram[0]=extras[0];b2l_owned_ram[1]=extras[1];b2l_owned_ram[2]=extras[2];b2l_owned_ram[3]=(struct b2l_ram_range){0x900000000ULL,4024ULL<<20};
 assert(!b2l_ram_validate(b2l_owned_ram,4,0x900000000ULL,4024ULL<<20));
 assert(!b2l_managed_pa(0x87000000ULL)&&!b2l_managed_pa(0xca000000ULL)&&!b2l_managed_pa(0x880000000ULL));
 unsigned count=0;for(unsigned r=0;r<4;r++)for(uint64_t p=b2l_owned_ram[r].base;p<b2l_owned_ram[r].base+b2l_owned_ram[r].size;p+=PAGE_SIZE){
  assert(b2l_ram_pai(b2l_owned_ram,4,p)==count);assert(b2l_ram_pa(b2l_owned_ram,4,count)==p);count++;
 }
 assert(b2l_ram_pai(b2l_owned_ram,4,0x9fb800000ULL)==count);assert(b2l_ram_pa(b2l_owned_ram,4,count)==0);
 assert(b2l_ram_pai(b2l_owned_ram,4,0x87000000)==~0u);
 /* Native iterator excludes physical gaps AND the primary bootstrap prefix. */
 gPhysBase=0x900000000ULL;first_avail=gPhysBase+(128ULL<<20);
 unsigned expected=count-(128ULL<<20)/PAGE_SIZE;assert(pmap_free_pages()==expected);
 ppnum_t pn;uint64_t last=0;unsigned seen=0;
 while(pmap_next_page(&pn)){uint64_t pa=pn*PAGE_SIZE;assert(pa>last&&b2l_managed_pa(pa));assert(pa<gPhysBase||pa>=gPhysBase+(128ULL<<20));last=pa;seen++;}
 assert(seen==expected&&!pmap_free_pages());
 ptov_table[0].pa=0xb4800000;ptov_table[0].va=0x1000000000ULL;ptov_table[0].len=16384;
 ptov_table[1].pa=0xb4804000;ptov_table[1].va=0x2000000000ULL;ptov_table[1].len=16384;
 b2l_invalidate_icache_phys(0xb4803ff0,32);assert(icalls==2&&iva[0]==0x1000003ff0ULL&&iva[1]==0x2000000000ULL&&ilen[0]==16&&ilen[1]==16);
 puts("Discontiguous RAM: EFI ownership/holes/runtime/GOP/EL2, dense roundtrips and native iterator passed");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(program)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(R/'common/memory'),'-I'+str(R/'common/smp'),'-I'+str(R/'uefi/XnuLoader'),str(p/'test.c'),str(R/'uefi/XnuLoader/Arena.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
# Audit actual firmware profile, independently from the C allowlist.
spec=importlib.util.spec_from_file_location('prep',R/'scripts/prepare-boot-firmware.py');prep=importlib.util.module_from_spec(spec);spec.loader.exec_module(prep)
ref=Path('/Users/orhuntx/UEFI_beyond2lte/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Library/MemoryMapLib/MemoryMapLib.c').read_text()
profile=prep.xnu_memory_profile(ref);assert prep.xnu_memory_profile(profile)==profile
assert not re.search(r'^\s*\{"(?:Lower dynamic guard|Upper bank guard)"',profile,re.M)
spans=[(int(a,16),int(b,16)) for a,b in re.findall(r'^\s*\{"[^"]+",\s*(0x[0-9a-fA-F]+)(?:ULL)?,\s*(0x[0-9a-fA-F]+)(?:ULL)?,.*?Reserv,',profile,re.M)]
nodes=json.loads((R/'common/memory/beyond2lte-8g-dt.json').read_text());fixed=0
for name,node in nodes.items():
 if not name.startswith('/reserved-memory/') or 'reg' not in node:continue
 raw=bytes.fromhex(node['reg']);b=int.from_bytes(raw[:8],'big');n=int.from_bytes(raw[8:],'big');fixed+=1
 assert any(a<=b and a+z>=b+n for a,z in spans),name
print('Firmware profile: all',fixed,'fixed runtime-DT reservations retained; broad budget guards removed')
