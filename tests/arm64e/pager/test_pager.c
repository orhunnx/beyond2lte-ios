#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define CONFIG_SPTM 0
#if __has_feature(ptrauth_calls)
#error Host must compile as ordinary arm64, not arm64e.
#endif
#define PAGE_SIZE 4096u
#define PAGE_SHIFT 12
#define PAGE_MASK (PAGE_SIZE-1)
#define TRUE 1
#define FALSE 0
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define Z_WAITOK 0
#define VM_PROT_READ 1
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define KERN_MEMORY_ERROR 10
#define KERN_ALREADY_WAITING 11
#define MACH_SEND_INTERRUPTED 12
#define KERN_RESOURCE_SHORTAGE 13
#define KERN_NOT_SUPPORTED 14
#define VM_PAGE_NULL NULL
#define VM_PAGE_OBJECT(p) ((p)->object)
#define VM_PAGE_GET_PHYS_PAGE(p) ((p)->physical)
#define VM_PAGE_FREE(p) free_top(p)
#define bcopy(s,d,n) memcpy(d,s,n)
typedef int kern_return_t, vm_fault_return_t,vm_prot_t,boolean_t;
typedef uint64_t vm_object_offset_t,pmap_paddr_t,mach_vm_offset_t;
typedef uintptr_t vm_offset_t;
enum { VM_FAULT_SUCCESS, VM_FAULT_RETRY, VM_FAULT_MEMORY_SHORTAGE,
 VM_FAULT_INTERRUPTED,VM_FAULT_SUCCESS_NO_VM_PAGE,VM_FAULT_BUSY,VM_FAULT_MEMORY_ERROR };
struct object { int locked,paging_in_progress,code_signed; };
typedef struct object *vm_object_t;
struct page {vm_object_t object;unsigned physical,vmp_busy,vmp_cs_validated,vmp_cs_tainted,vmp_cs_nx;};
typedef struct page *vm_page_t;
struct vm_object_fault_info { uint64_t lo_offset,hi_offset;int interruptible; };
struct slide_entry { uint32_t version,page_size,page_starts_count;uint64_t value_add;uint16_t page_starts[2]; };
typedef struct slide_entry *vm_shared_region_slide_info_entry_v3_t,*vm_shared_region_slide_info_entry_v5_t;
union slide_union { struct {uint32_t version,page_size;};struct slide_entry v3,v5;};
struct slide_info {uint32_t si_slide;uint64_t si_slid_address,si_start,si_end;union slide_union *si_slide_info_entry;};
typedef struct slide_info *vm_shared_region_slide_info_t;
struct shared_pager {vm_shared_region_slide_info_t srp_slide_info;};
typedef struct shared_pager *shared_region_pager_t;
#define DYLD_CACHE_SLIDE_V3_PAGE_ATTR_NO_REBASE 0xffff
#define DYLD_CACHE_SLIDE_V5_PAGE_ATTR_NO_REBASE 0xffff
static struct object original,shadow;
static struct page pages[4],top;
static uint8_t *physical[4];
static unsigned phys_ids[]={101,7,800,42};
static uint64_t offsets[4],base,expected_lo,expected_hi;
static unsigned calls,wakes,top_frees,validations,allocs,slide_calls;
static int enabled=1,allocation_fail,shadowed,mutate_info,wait_result;
static int statuses[32],errors[32];static unsigned status_count;
static int b2l_arm64e_compat_enabled(void) {return enabled;}
static void vm_page_grab_prime(void) {}
static void vm_object_lock(vm_object_t o) {assert(!o->locked);o->locked=1;}
static void vm_object_unlock(vm_object_t o) {assert(o->locked);o->locked=0;}
static void vm_object_paging_begin(vm_object_t o) {assert(o->locked);o->paging_in_progress++;}
static void vm_object_paging_end(vm_object_t o) {assert(o->locked && o->paging_in_progress>0);o->paging_in_progress--;}
static void free_top(vm_page_t p) {assert(p==&top && p->vmp_busy && p->object->locked);p->vmp_busy=0;top_frees++;}
static void vm_page_wakeup_done(vm_object_t o,vm_page_t p) {assert(o==p->object && o->locked && p->vmp_busy);p->vmp_busy=0;wakes++;}
static int vm_page_wait(int interruptible) {assert(interruptible==1);return wait_result;}
static void *phystokv(pmap_paddr_t a) {assert(!(a&PAGE_MASK));for(unsigned i=0;i<4;i++)if((a>>PAGE_SHIFT)==phys_ids[i])return physical[i];assert(!"bad physical page");return NULL;}
static void vm_page_validate_cs_mapped(vm_page_t p,unsigned size,unsigned off,const void *a) {assert(p->object->locked && p->vmp_busy && size==PAGE_SIZE && off==0 && a==phystokv((uint64_t)p->physical<<PAGE_SHIFT));validations++;}
static void *kalloc_data(size_t n,int flags) {assert(n==16384 && flags==Z_WAITOK);if(allocation_fail)return NULL;void *p=malloc(n);assert(p);allocs++;return p;}
static void kfree_data(void *p,size_t n) {assert(p && n==16384 && allocs);allocs--;free(p);}
static vm_fault_return_t vm_fault_page(vm_object_t o,uint64_t off,int access,int a,int b,vm_prot_t *prot,vm_page_t *page,vm_page_t *tp,void *x,kern_return_t *error,int c,struct vm_object_fault_info *info,void *z) {
 assert(o==&original && o->locked && o->paging_in_progress==1 && access==VM_PROT_READ && *prot==VM_PROT_READ);
 assert(!*page && !*tp);
 /* Fresh-copy regression: mutate the same fields real shadow traversal mutates. */
 assert(info->lo_offset==expected_lo && info->hi_offset==expected_hi);
 if(mutate_info){info->lo_offset+=0x4000;info->hi_offset+=0x4000;}
 unsigned index=calls++;int result=index<status_count?statuses[index]:VM_FAULT_SUCCESS;
 if(index<status_count)*error=errors[index];
 if(result!=VM_FAULT_SUCCESS){
  if(result!=VM_FAULT_SUCCESS_NO_VM_PAGE){vm_object_paging_end(o);vm_object_unlock(o);}
  return result;
 }
 unsigned p=4;for(unsigned i=0;i<4;i++)if(off==base+4096*i)p=i;assert(p<4);offsets[index%4]=off;
 vm_object_t owner=o;
 if(shadowed){assert(!top.vmp_busy);top.object=o;top.vmp_busy=1;*tp=&top;vm_object_unlock(o);owner=&shadow;vm_object_lock(owner);vm_object_paging_begin(owner);}
 assert(!pages[p].vmp_busy);pages[p].object=owner;pages[p].vmp_busy=1;*page=&pages[p];return result;
}
#include "actual_slide.h"
static kern_return_t vm_shared_region_slide_page(vm_shared_region_slide_info_t si,vm_offset_t a,mach_vm_offset_t u,uint32_t index,uint64_t key) {
 slide_calls++;assert(key==0 && u==si->si_slid_address+(uint64_t)index*16384);
 if(si->si_slide_info_entry->version==3)return vm_shared_region_slide_page_v3(si,a,u,index,key);
 if(si->si_slide_info_entry->version==5)return vm_shared_region_slide_page_v5(si,a,u,index,key);
 return KERN_FAILURE;
}
#include "actual_helpers.h"
static void reset(void) {
 memset(&original,0,sizeof(original));memset(&shadow,0,sizeof(shadow));memset(pages,0,sizeof(pages));memset(&top,0,sizeof(top));
 original.code_signed=shadow.code_signed=1;calls=wakes=top_frees=validations=allocs=slide_calls=0;
 enabled=1;allocation_fail=shadowed=mutate_info=wait_result=0;status_count=0;memset(statuses,0,sizeof(statuses));memset(errors,0,sizeof(errors));
 base=0x23000;expected_lo=base;expected_hi=base+16384;
 for(unsigned i=0;i<4;i++){pages[i].physical=phys_ids[i];pages[i].vmp_cs_validated=1;pages[i].vmp_cs_nx=i&1;memset(physical[i],0x90+i,4096);}
}
static void clean(void) {assert(!original.locked && !shadow.locked && !original.paging_in_progress && !shadow.paging_in_progress && !top.vmp_busy && !allocs);for(unsigned i=0;i<4;i++)assert(!pages[i].vmp_busy);}
static void put(unsigned offset,uint64_t value) {assert(offset<=16384-8);memcpy(physical[offset/4096]+offset%4096,&value,8);}
static union slide_union entry;
static struct slide_info si;
static struct shared_pager pager;
static struct vm_object_fault_info info;
static void setup(unsigned version) {
 reset();memset(&entry,0,sizeof(entry));entry.version=version;entry.page_size=16384;entry.v5.page_starts_count=1;entry.v5.value_add=UINT64_C(0x180000000);entry.v5.page_starts[0]=4088;
 si=(struct slide_info){0x4000,UINT64_C(0x180023000),base,base+16384,&entry};pager.srp_slide_info=&si;info=(struct vm_object_fault_info){expected_lo,expected_hi,1};
}
static void chain(unsigned version) {
 const unsigned at[]={4088,4096,8192,12288,16376};
 for(unsigned n=0;n<5;n++) {
  uint64_t value=0x1000+n;unsigned delta=n==4?0:(at[n+1]-at[n])/8;
  if(version==5)value|=(uint64_t)delta<<52;else value|=(uint64_t)delta<<51;
  /* Alternate authenticated/unauthed records. */
  if(n&1)value|=UINT64_C(1)<<63;put(at[n],value);
 }
}
static void test_slices(void) {
 const unsigned at[]={4088,4096,8192,12288,16376};
 for(unsigned version=3;version<=5;version+=2)for(unsigned slice=0;slice<4;slice++)for(unsigned sh=0;sh<2;sh++) {
  setup(version);shadowed=sh;mutate_info=1;chain(version);pages[2].vmp_cs_tainted=1;pages[1].vmp_cs_validated=0;
  uint8_t output[4096],expected[16384],before[16384];for(unsigned i=0;i<4;i++)memcpy(before+i*4096,physical[i],4096);memcpy(expected,before,sizeof(expected));
  for(unsigned n=0;n<5;n++){uint64_t v=0x1000+n+si.si_slide;if((n&1)||version==5)v+=entry.v5.value_add;memcpy(expected+at[n],&v,8);}
  unsigned valid=99,taint=99,nx=99;
  assert(b2l_shared_region_copy_large(&pager,&original,base+slice*4096,(uintptr_t)output,&info,&valid,&taint,&nx)==0);
  assert(!memcmp(output,expected+slice*4096,4096));assert(valid==0 && taint==1 && nx==(slice&1));
  assert(calls==4 && wakes==4 && validations==4 && top_frees==4*sh && slide_calls==1);
  for(unsigned i=0;i<4;i++)assert(!memcmp(before+i*4096,physical[i],4096));
  assert(info.lo_offset==expected_lo && info.hi_offset==expected_hi);clean();
 }
}
static void assert_failure(kern_return_t expected) {
 uint8_t out[4096];memset(out,0xa7,sizeof(out));unsigned v=7,t=8,n=9;
 assert(b2l_shared_region_copy_large(&pager,&original,base,(uintptr_t)out,&info,&v,&t,&n)==expected);
 for(unsigned i=0;i<4096;i++)assert(out[i]==0xa7);assert(v==7&&t==8&&n==9);clean();
}
static void test_bounds_and_failures(void) {
 setup(5);si.si_end-=4096;assert_failure(KERN_FAILURE);assert(calls==0);
 setup(5);enabled=0;assert_failure(KERN_FAILURE);assert(calls==0);
 setup(5);allocation_fail=1;assert_failure(KERN_RESOURCE_SHORTAGE);
 setup(5);entry.page_size=8192;assert_failure(KERN_FAILURE);
 setup(5);si.si_start++;assert_failure(KERN_FAILURE);
 setup(5);si.si_start=si.si_end+4096;assert_failure(KERN_FAILURE);
 setup(5);si.si_start=0;si.si_end=(UINT64_C(0x100000002)*16384);
 uint8_t huge_out[4096];unsigned hv=7,ht=8,hn=9;memset(huge_out,0x41,sizeof(huge_out));
 assert(b2l_shared_region_copy_large(&pager,&original,UINT64_C(0x100000000)*16384,(uintptr_t)huge_out,&info,&hv,&ht,&hn)==KERN_FAILURE);
 assert(calls==0&&hv==7&&ht==8&&hn==9);clean();
 setup(5);entry.v5.page_starts_count=0;assert_failure(KERN_FAILURE);
 setup(5);entry.v5.page_starts[0]=16380;assert_failure(KERN_FAILURE);
 setup(5);entry.v5.page_starts[0]=16376;put(16376,UINT64_C(1)<<52);assert_failure(KERN_FAILURE);
 setup(5);put(4088,UINT64_C(2047)<<52);assert_failure(KERN_FAILURE);
 setup(3);put(4088,UINT64_C(1)<<62);assert_failure(KERN_FAILURE);
 setup(4);assert_failure(KERN_FAILURE);
 /* Allocate/read errors at EACH source page must not publish partial output. */
 for(unsigned p=0;p<4;p++){setup(5);chain(5);shadowed=1;status_count=p+1;statuses[p]=VM_FAULT_MEMORY_ERROR;errors[p]=123;assert_failure(123);assert(wakes==p && top_frees==p);}
 setup(5);entry.v5.page_starts[0]=0xffff;uint8_t out[4096];unsigned v=0,t=0,n=0;
 assert(b2l_shared_region_copy_large(&pager,&original,base+4096,(uintptr_t)out,&info,&v,&t,&n)==0);assert(!memcmp(out,physical[1],4096));clean();
 /* Outside slide range: copy one raw page, no slide/no scratch. */
 setup(5);si.si_start=base+4096;si.si_end=base+16384;
 assert(b2l_shared_region_copy_large(&pager,&original,base,(uintptr_t)out,&info,&v,&t,&n)==0);assert(calls==1&&slide_calls==0&&wakes==1);assert(!memcmp(out,physical[0],4096));clean();
}
static void test_unsigned_source(void) {
 setup(5);chain(5);original.code_signed=0;
 for(unsigned i=0;i<4;i++){pages[i].vmp_cs_validated=0;pages[i].vmp_cs_tainted=0;pages[i].vmp_cs_nx=0;}
 uint8_t out[4096];unsigned v=99,t=99,n=99;
 assert(b2l_shared_region_copy_large(&pager,&original,base,(uintptr_t)out,&info,&v,&t,&n)==0);
 assert(v==0&&t==0&&n==0&&validations==0);clean();
}
static void test_fault_contracts(void) {
 const int results[]={VM_FAULT_RETRY,VM_FAULT_MEMORY_SHORTAGE,VM_FAULT_INTERRUPTED,VM_FAULT_SUCCESS_NO_VM_PAGE,VM_FAULT_BUSY,VM_FAULT_MEMORY_ERROR};
 const int expected[]={0,0,MACH_SEND_INTERRUPTED,KERN_MEMORY_ERROR,KERN_ALREADY_WAITING,KERN_MEMORY_ERROR};
 for(unsigned s=0;s<6;s++)for(unsigned sh=0;sh<2;sh++) {
  setup(5);shadowed=sh;mutate_info=1;status_count=1;statuses[0]=results[s];wait_result=1;
  unsigned v=7,t=8,n=9;uint8_t out[4096];memset(out,0xa9,sizeof(out));
  int kr=b2l_shared_region_read_page(&original,base,out,&info,&v,&t,&n);assert(kr==expected[s]);
  if(!kr){assert(!memcmp(out,physical[0],4096));assert(calls==2&&wakes==1&&top_frees==sh);}
  else {for(unsigned i=0;i<4096;i++)assert(out[i]==0xa9);assert(v==7&&t==8&&n==9);}
  assert(info.lo_offset==expected_lo&&info.hi_offset==expected_hi);clean();
 }
 setup(5);status_count=1;statuses[0]=VM_FAULT_MEMORY_SHORTAGE;wait_result=0;assert_failure(MACH_SEND_INTERRUPTED);
}
int main(void) {
 for(unsigned i=0;i<4;i++){physical[i]=malloc(4096);assert(physical[i]);}
 test_slices();test_bounds_and_failures();test_fault_contracts();test_unsigned_source();
 for(unsigned i=0;i<4;i++)free(physical[i]);
 puts("actual mixed-pager helpers + actual V3/V5 slide: discontiguous4K, all slices, chain bounds, VMfault/toprefs, CSflags, transactional faults and shadowRETRY passed");return 0;
}
