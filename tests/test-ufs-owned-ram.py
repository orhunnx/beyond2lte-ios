#!/usr/bin/env python3
"""Run the actual native UFS initializer with mocked kernel allocation and MMIO.

Regression: allocator-owned DMA below/outside the primary arena must reach
controller takeover; holes, overflow and noncontiguous mappings must not.
"""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'xnu-overlay/bsd/dev/b2l_ufs_transport.c').read_text()
a=s.index('int b2l_ufs_transport_init(');b=s.index('\nint b2l_ufs_transport_read(',a)
initializer=s[a:b]
program=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "b2l_ufs_protocol.h"
#include "b2l_ram_ranges.h"
typedef uint64_t vm_offset_t,ppnum_t,addr64_t;
typedef void *DTEntry;
#define PAGE_SIZE 16384
#define PAGE_MASK (PAGE_SIZE-1)
#define ptoa_64(x) ((uint64_t)(x)*PAGE_SIZE)
#define KMA_DATA 1
#define KMA_ZERO 2
#define KMA_PERMANENT 4
#define VM_KERN_MEMORY_IOKIT 1
#define KERN_SUCCESS 0
#define kSuccess 0
#define NSEC_PER_SEC 1000000000ULL
static int b2l_ufs_mutex,kernel_map,kernel_pmap;
static struct {
 vm_offset_t hci,vs,dma;uint64_t dma_pa;
 struct b2l_ufs_io io;struct b2l_ufs_geometry geometry;
 int initialized,error,poisoned,write_reported;
} host;
static struct b2l_ram_range b2l_owned_ram[B2L_RAM_MAX_RANGES];
static unsigned b2l_owned_ram_count;
static uint32_t lun;static uint64_t allocation;static int discontinuity,takeovers;
static void lck_mtx_lock(int*x){(void)x;}
static void lck_mtx_unlock(int*x){(void)x;}
static int SecureDTLookupEntry(void*x,const char*y,DTEntry*z){(void)x;(void)y;*z=0;return 0;}
static int SecureDTGetProperty(DTEntry x,const char*y,const void**p,unsigned*n){(void)x;(void)y;*p=&lun;*n=4;return 0;}
static vm_offset_t ml_io_map(vm_offset_t p,vm_offset_t n){(void)n;return p;}
static int kmem_alloc_contig(int m,vm_offset_t*p,unsigned n,unsigned a,int b,int c,int d,int e){(void)m;(void)n;(void)a;(void)b;(void)c;(void)d;(void)e;*p=0x100000000ULL;return 0;}
static ppnum_t pmap_find_phys(int m,addr64_t va){(void)m;uint64_t off=va-0x100000000ULL;return allocation/PAGE_SIZE+off/PAGE_SIZE+(discontinuity&&off?1:0);}
static uint32_t native_read(void*p,uint32_t v){(void)p;return v;}
static void native_write(void*p,uint32_t a,uint32_t b){(void)p;(void)a;(void)b;}
static uint64_t native_clock(void*p){(void)p;return 0;}
static void native_pause(void*p){(void)p;}
static void native_sync(void*p,int v){(void)p;(void)v;}
static void nanoseconds_to_absolutetime(uint64_t a,uint64_t*b){*b=a;}
static int takeover(void){takeovers++;return 0;}
static int geometry(void){host.geometry.block_count=100;host.geometry.block_size=4096;return 0;}
''' + initializer + r'''
static void run(uint64_t pa,int fragmented,int expected){
 memset(&host,0,sizeof(host));allocation=pa;discontinuity=fragmented;takeovers=0;
 struct b2l_ufs_geometry g={0};assert(b2l_ufs_transport_init(&g)==expected);
 assert(takeovers==(expected==0));
 if(!expected)assert(g.block_count==100&&g.block_size==4096);
}
int main(void){
 b2l_owned_ram_count=3;
 b2l_owned_ram[0]=(struct b2l_ram_range){0xb4800000ULL,0x6300000};
 b2l_owned_ram[1]=(struct b2l_ram_range){0x88ad00000ULL,0x75300000};
 b2l_owned_ram[2]=(struct b2l_ram_range){0x900000000ULL,4024ULL<<20};
 run(0xb4800000ULL,0,0); /* low DDR: used to be rejected */
 run(0x88ad00000ULL,0,0); /* extra upper DDR: used to be rejected */
 run(0x900000000ULL,0,0); /* original primary remains supported */
 run(0x880000000ULL,0,EFAULT); /* firmware reservation */
 run(0xbab00000ULL-16384,0,EFAULT); /* extent crosses bank end */
 run(0xffffffffffffc000ULL,0,EFAULT); /* physical end overflows */
 run(0xb4800000ULL,1,EFAULT); /* owned but not physically contiguous */
 puts("Native UFS init: extra/primary banks accepted; holes/overflow/noncontiguity rejected before takeover");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(program)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(r/'xnu-overlay/bsd/dev'),'-I'+str(r/'common/memory'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
