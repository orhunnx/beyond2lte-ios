/* Actual runtime TU with mock VM/copyin/lock/entropy boundary; decoder and
 * QARMA are the shipped sources, not duplicate test implementations. */
#define B2L_COMPAT_HOST_TEST 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define STARTUP(a,b,c)
#define MACH_VM_MAX_ADDRESS UINT64_C(0x1000000000)
#define VM_PROT_EXECUTE 4
#define VM_SANITIZE_CALLER_USERACC 1
#define KERN_SUCCESS 0
#define VM_REGION_SUBMAP_SHORT_INFO_COUNT_64 1
#define vm_sanitize_wrap_addr(x) (x)
#define vm_sanitize_wrap_size(x) (x)
#define VM_SANITIZE_UNSAFE_UNWRAP(x) (x)
typedef uint64_t vm_map_offset_ut, vm_map_size_ut;
typedef unsigned natural_t, mach_msg_type_number_t;
typedef int kern_return_t;
typedef struct { int protection; bool is_submap; } vm_region_submap_short_info_data_64_t;
typedef vm_region_submap_short_info_data_64_t *vm_region_submap_info_64_t;
#define LCK_GRP_DECLARE(a,b) static int a __unused
#define LCK_MTX_DECLARE(a,b) static pthread_mutex_t a=PTHREAD_MUTEX_INITIALIZER
#define lck_mtx_lock pthread_mutex_lock
#define lck_mtx_unlock pthread_mutex_unlock
struct arm_saved_state {uint64_t x[29],fp,lr,sp,pc;uint32_t cpsr;bool user64;};
typedef struct arm_saved_state arm_saved_state_t;
static bool task_compat=true, executable=true, copy_fault=false;
static unsigned entropy_calls, atomic32_calls;
static unsigned region_queries;
static int region_error;
static bool leaf_is_submap, region_count_bad;
static uint64_t leaf_start=0x1000, leaf_size=0x3000;
static uint8_t memory[0x4000];
static int boot_flag;
static bool PE_parse_boot_argn(const char *n,void *p,size_t z){assert(!strcmp(n,"b2l_arm64e_compat"));assert(z==sizeof(int));memcpy(p,&boot_flag,z);return true;}
static bool b2l_arm64e_current_task(void){return task_compat;}
static bool is_saved_state64(arm_saved_state_t*s){return s->user64;}
#define PSR64_IS_USER64(c) (((c)&15)==0)
static uint32_t get_saved_state_cpsr(arm_saved_state_t*s){return s->cpsr;}
static uint64_t get_saved_state_reg(arm_saved_state_t*s,unsigned i){assert(i<29);return s->x[i];}
static uint64_t get_saved_state_fp(arm_saved_state_t*s){return s->fp;}
static uint64_t get_saved_state_lr(arm_saved_state_t*s){return s->lr;}
static uint64_t get_saved_state_sp(arm_saved_state_t*s){return s->sp;}
static uint64_t get_saved_state_pc(arm_saved_state_t*s){return s->pc;}
static void set_user_saved_state_reg(arm_saved_state_t*s,unsigned i,uint64_t v){assert(i<29);s->x[i]=v;}
static void set_saved_state_fp(arm_saved_state_t*s,uint64_t v){s->fp=v;}
static void set_user_saved_state_lr(arm_saved_state_t*s,uint64_t v){s->lr=v;}
static void set_saved_state_sp(arm_saved_state_t*s,uint64_t v){s->sp=v;}
static void set_user_saved_state_pc(arm_saved_state_t*s,uint64_t v){s->pc=v;}
static void*current_map(void){return memory;}
static kern_return_t vm_map_region_recurse_64(void *m, vm_map_offset_ut *a,
 vm_map_size_ut *z, natural_t *depth, vm_region_submap_info_64_t info,
 mach_msg_type_number_t *count) {
 assert(m==memory && *depth==UINT32_MAX && *count==VM_REGION_SUBMAP_SHORT_INFO_COUNT_64);
 ++region_queries;
 if (region_error) return region_error;
 /* Parent is read-only; this API descends to an executable cache leaf. */
 *a=leaf_start; *z=leaf_size; *depth=1;
 info->protection=executable?VM_PROT_EXECUTE:1; info->is_submap=leaf_is_submap;
 if(region_count_bad) *count=0;
 return KERN_SUCCESS;
}
static int copyin(uint64_t a,void*out,unsigned n){if(copy_fault||a<0x1000||a>0x4000||n>0x4000-a)return -1;memcpy(out,memory+a,n);return 0;}
static int copyin_atomic32(uint64_t a,uint32_t*out){assert(!(a&3));++atomic32_calls;return copyin(a,out,4);}
static int copyin_atomic64(uint64_t a,uint64_t*out){assert(!(a&7));return copyin(a,out,8);}
static void read_frandom(void*out,unsigned n){assert(n==16);++entropy_calls;for(unsigned i=0;i<n;i++)((uint8_t*)out)[i]=(uint8_t)(i+71);}
static int memset_s(void*p,size_t z,int v,size_t n){assert(n<=z);memset(p,v,n);return 0;}
#include "b2l_pacga_qarma5.h"
uint64_t b2l_pacga_qarma5_asm(uint64_t a,uint64_t b,uint64_t c,uint64_t d){return b2l_pacga_qarma5(a,b,c,d);}
static unsigned early_log_bytes;
static void b2l_early_trace(const char *fmt, ...){assert(fmt);}
static void b2l_early_putc(char c){assert(c);++early_log_bytes;}
#define printf(...) do { assert(!"normal printf forbidden during TUNABLES"); } while(0)
#include "b2l_arm64e_compat.c"
#undef printf

static arm_saved_state_t initial(void){arm_saved_state_t s={0};for(unsigned i=0;i<29;i++)s.x[i]=i+100;s.fp=300;s.lr=0x1800;s.sp=0x2000;s.pc=0x1000;s.user64=true;s.cpsr=0xa0000000;return s;}
static void rejected_unchanged(arm_saved_state_t s,uint32_t op){arm_saved_state_t old=s;assert(!b2l_arm64e_compat_emulate(&s,op));assert(!memcmp(&s,&old,sizeof(s)));}
int main(void){
 arm_saved_state_t s=initial();
 boot_flag=0;b2l_compat_init();assert(!early_log_bytes);rejected_unchanged(s,0xd65f0bff);
 boot_flag=2;b2l_compat_init();assert(!early_log_bytes);rejected_unchanged(s,0xd65f0bff);
 boot_flag=1;b2l_compat_init();assert(b2l_arm64e_compat_enabled()&&early_log_bytes);
 task_compat=false;rejected_unchanged(s,0xd65f0bff);task_compat=true;
 s.user64=false;rejected_unchanged(s,0xd65f0bff);s=initial();
 s.cpsr|=4;rejected_unchanged(s,0xd65f0bff);s=initial();
 executable=false;rejected_unchanged(s,0xd65f0bff);executable=true;
 copy_fault=true;rejected_unchanged(s,0xd65f0bff);copy_fault=false;
 rejected_unchanged(s,0xffffffff);
 s.lr=UINT64_C(0xfffffff007000000);rejected_unchanged(s,0xd65f0bff);
 s=initial();s.lr=0x1802;rejected_unchanged(s,0xd65f0bff);
 s=initial();assert(b2l_arm64e_compat_emulate(&s,0xd65f0bff));assert(s.pc==0x1800&&s.cpsr==0xa0000000&&s.fp==300&&s.sp==0x2000&&s.x[28]==128);
 s=initial();unsigned before_atomic32=atomic32_calls;assert(b2l_arm64e_compat_emulate(&s,0xd65f0fff));assert(s.pc==0x1800&&atomic32_calls==before_atomic32+2);
 /* BLRAA x3,x4 updates LR once without touching FP/SP/flags. */
 s=initial();s.x[3]=0x1800;assert(b2l_arm64e_compat_emulate(&s,0xd73f0864));assert(s.pc==0x1800&&s.lr==0x1004&&s.cpsr==0xa0000000);
 /* Exact physical failure: BLRAA x8,x16 into an executable shared-cache leaf. */
 s=initial();s.x[8]=0x1800;s.x[16]=UINT64_C(0x81c7000000002000);
 unsigned before_queries=region_queries;
 assert(b2l_arm64e_compat_emulate(&s,0xd73f0910));
 assert(s.pc==0x1800&&s.lr==0x1004&&s.x[16]==UINT64_C(0x81c7000000002000));
 assert(region_queries==before_queries+2);
 s=initial();s.x[8]=0x1800;
 executable=false;rejected_unchanged(s,0xd73f0910);executable=true;
 region_error=1;rejected_unchanged(s,0xd73f0910);region_error=0;
 leaf_is_submap=true;rejected_unchanged(s,0xd73f0910);leaf_is_submap=false;
 region_count_bad=true;rejected_unchanged(s,0xd73f0910);region_count_bad=false;
 leaf_start=0x1004;rejected_unchanged(s,0xd73f0910);leaf_start=0x1000;
 leaf_size=3;rejected_unchanged(s,0xd73f0910);leaf_size=0x3000;
 s.x[8]=0x3000;leaf_size=0x2000;rejected_unchanged(s,0xd73f0910);leaf_size=0x3000;
 s.x[8]=UINT64_C(0xfffffff007000000);rejected_unchanged(s,0xd73f0910);
 /* PACGA x0,x1,x2: persistent random software key, exact full result. */
 uint32_t pacga=0x9ac03000|(2u<<16)|(1u<<5);s=initial();assert(b2l_arm64e_compat_emulate(&s,pacga));uint64_t hash=s.x[0];assert(!(hash&UINT32_MAX)&&entropy_calls==1);s=initial();assert(b2l_arm64e_compat_emulate(&s,pacga)&&s.x[0]==hash&&entropy_calls==1);
 uint64_t out=0;uint64_t value=UINT64_C(0x8877665544332211);memcpy(memory+0x2000,&value,8);
 assert(!b2l_compat_read_acquire(0x2000,8,&out,NULL)&&out==value);
 assert(!b2l_compat_read_acquire(0x2000,4,&out,NULL)&&out==0x44332211);
 assert(!b2l_compat_read_acquire(0x2002,2,&out,NULL)&&out==0x4433);
 assert(!b2l_compat_read_acquire(0x2003,1,&out,NULL)&&out==0x44);
 assert(b2l_compat_read_acquire(0x2001,2,&out,NULL));assert(b2l_compat_read_acquire(0x2000,0,&out,NULL));
 assert(!b2l_user_range(0,4)&&!b2l_user_range(UINT64_MAX-1,8)&&!b2l_user_range(MACH_VM_MAX_ADDRESS,4));
 copy_fault=true;assert(b2l_compat_read_acquire(0x2000,8,&out,NULL));copy_fault=false;
 puts("B2L runtime actual-source boundary tests PASS");return 0;
}
