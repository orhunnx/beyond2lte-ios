/* Actual runtime TU with mock VM/copyin/lock/entropy boundary; decoder and
 * QARMA are the shipped sources, not duplicate test implementations. */
#define B2L_COMPAT_HOST_TEST 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <net/siphash.h>
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define STARTUP(a,b,c)
#define PSR64_SS (1u<<21)
static struct host_thread {struct {void *DebugData;} machine;} host_thread;
static struct host_thread *current_thread(void){return &host_thread;}
#define MACH_VM_MAX_ADDRESS UINT64_C(0x1000000000)
#define VM_PROT_EXECUTE 4
#define VM_SANITIZE_CALLER_USERACC 1
#define KERN_SUCCESS 0
typedef int kern_return_t;
typedef uint64_t vm_map_address_t;
typedef void *vm_map_t;
struct mock_entry {int protection;bool is_sub_map;uint64_t vme_start,vme_end;};
typedef struct mock_entry *vm_map_entry_t;
struct mock_lock_ctx {uint64_t start,end,parent_start,parent_end;};
typedef struct mock_lock_ctx *vm_map_lock_ctx_t;
#define VM_MAP_LOCK_CTX_DECLARE(name) struct mock_lock_ctx name##_storage={0};vm_map_lock_ctx_t name=&name##_storage
#define VMRL_SH_STREAM_NO_HOLES 1u
#define VMRL_SH_DESCEND_INTO_CONSTANT 2u
#define LCK_GRP_DECLARE(a,b) static int a __unused
#define LCK_MTX_DECLARE(a,b) static pthread_mutex_t a=PTHREAD_MUTEX_INITIALIZER
static unsigned lock_calls;
static int test_mtx_lock(pthread_mutex_t *p){__atomic_fetch_add(&lock_calls,1,__ATOMIC_RELAXED);return pthread_mutex_lock(p);}
#define lck_mtx_lock test_mtx_lock
#define lck_mtx_unlock pthread_mutex_unlock
struct arm_saved_state {uint64_t x[29],fp,lr,sp,pc;uint32_t cpsr;bool user64;};
typedef struct arm_saved_state arm_saved_state_t;
static bool task_compat=true, executable=true, copy_fault=false;
static unsigned entropy_calls, atomic32_calls, atomic32_fail_at;
static uint64_t host_ticks;
static uint64_t mach_absolute_time(void){return __atomic_add_fetch(&host_ticks,1,__ATOMIC_RELAXED);}
static unsigned region_queries;
static int region_error;
static bool leaf_is_submap, lookup_entry_missing;
static int iterator_error;
static unsigned map_locked, map_locks, map_unlocks;
static uint64_t child_shift;
static uint64_t leaf_start=0x1000, leaf_size=0x3000;
static uint8_t memory[0x4000];
static int boot_flag, siphash_flag = -1, pair_flag = -1;
static bool PE_parse_boot_argn(const char *n,void *p,size_t z){assert(z==sizeof(int));if(!strcmp(n,"b2l_arm64e_compat")){memcpy(p,&boot_flag,z);return true;}if(!strcmp(n,"b2l_pacga_siphash")){if(siphash_flag<0)return false;memcpy(p,&siphash_flag,z);return true;}assert(!strcmp(n,"b2l_pacga_pair"));if(pair_flag<0)return false;memcpy(p,&pair_flag,z);return true;}
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
/* VM boundary mocks: actual range-lock/bounds API contract, including the
 * map-pointer invalidation and child-to-parent bounds translation. The parent
 * can be read-only while its constant-submap leaf is executable. */
static kern_return_t vm_map_range_sh_lock(vm_map_lock_ctx_t ctx,vm_map_t *map,
 uint64_t start,uint64_t end,unsigned flags){
 assert(*map==memory&&end==start+4&&flags==(VMRL_SH_STREAM_NO_HOLES|VMRL_SH_DESCEND_INTO_CONSTANT));
 assert(!map_locked);++region_queries;if(region_error)return region_error;
 ctx->start=start;ctx->end=end;ctx->parent_start=leaf_start;ctx->parent_end=leaf_start+leaf_size;
 *map=NULL;map_locked=1;++map_locks;return KERN_SUCCESS;
}
static vm_map_entry_t vm_map_range_stream_next_with_error(vm_map_lock_ctx_t ctx,kern_return_t *kr){
 static struct mock_entry entry;assert(map_locked&&ctx->end==ctx->start+4);
 *kr=iterator_error;if(iterator_error||lookup_entry_missing)return NULL;
 entry=(struct mock_entry){executable?VM_PROT_EXECUTE:1,leaf_is_submap,leaf_start+child_shift,leaf_start+leaf_size+child_shift};return &entry;
}
static void vm_map_lock_ctx_bounds_in_parent(vm_map_lock_ctx_t ctx,uint64_t *start,uint64_t *end,void *size){
 assert(map_locked&&!size);*start=ctx->parent_start>ctx->start?ctx->parent_start:ctx->start;
 *end=ctx->parent_end<ctx->end?ctx->parent_end:ctx->end;
}
static void vm_map_range_sh_unlock(vm_map_lock_ctx_t ctx,vm_map_t *map){
 assert(map_locked&&!*map&&ctx);map_locked=0;*map=memory;++map_unlocks;
}
static int copyin(uint64_t a,void*out,unsigned n){assert(!map_locked);if(copy_fault||a<0x1000||a>0x4000||n>0x4000-a)return -1;memcpy(out,memory+a,n);return 0;}
static int copyin_atomic32(uint64_t a,uint32_t*out){assert(!(a&3));++atomic32_calls;if(atomic32_calls==atomic32_fail_at)return -1;return copyin(a,out,4);}
static int copyin_atomic64(uint64_t a,uint64_t*out){assert(!(a&7));return copyin(a,out,8);}
static void read_frandom(void*out,unsigned n){assert(n==16);++entropy_calls;for(unsigned i=0;i<n;i++)((uint8_t*)out)[i]=(uint8_t)(i+71);}
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

/* Exercise the actual callback, including concurrent first-key publication.
 * Each thread models a different task/CPU, using identical and changing inputs.
 * Compare against the existing independent XNU SipHash KAT and C QARMA path. */
static unsigned race_ready, race_go;
static uint64_t expected_hash(uint64_t a,uint64_t b){
 uint64_t key[2];for(unsigned i=0;i<16;i++)((uint8_t*)key)[i]=(uint8_t)(i+71);
 if(b2l_compat_siphash){SIPHASH_CTX ctx;uint64_t msg[2]={a,b};return SipHashX(&ctx,2,4,(uint8_t*)key,msg,16)&UINT64_C(0xffffffff00000000);}
 return b2l_pacga_qarma5(a,b,key[0],key[1]);
}
static void *race_worker(void *unused){
 (void)unused;__atomic_fetch_add(&race_ready,1,__ATOMIC_RELEASE);
 while(!__atomic_load_n(&race_go,__ATOMIC_ACQUIRE))sched_yield();
 for(unsigned i=0;i<10000;i++){
  uint64_t a=UINT64_C(0x123456789abc0000)+(i%71),b=(i&1)?0x82c:UINT64_C(0xffff001122334455),v=0;
  assert(!b2l_compat_pacga(a,b,&v,NULL));assert(v==expected_hash(a,b));assert(!(v&UINT32_MAX));
 }
 return NULL;
}
static void key_race_test(void){
 pthread_t workers[8];
 for(unsigned i=0;i<8;i++)assert(!pthread_create(&workers[i],NULL,race_worker,NULL));
 while(__atomic_load_n(&race_ready,__ATOMIC_ACQUIRE)!=8)sched_yield();
 __atomic_store_n(&race_go,1,__ATOMIC_RELEASE);
 for(unsigned i=0;i<8;i++)assert(!pthread_join(workers[i],NULL));
 assert(entropy_calls==1);unsigned before=lock_calls;
 for(unsigned i=0;i<1000;i++){uint64_t v;assert(!b2l_compat_pacga(i,0x82c,&v,NULL));assert(v==expected_hash(i,0x82c));}
 assert(lock_calls==before);assert(b2l_compat_pacga(1,2,NULL,NULL)==-1);
}

static uint32_t pacga_word(unsigned rd,unsigned rn,unsigned rm){return UINT32_C(0x9ac03000)|(rm<<16)|(rn<<5)|rd;}
static void expected_step(arm_saved_state_t *s,uint32_t op){
 unsigned rn=(op>>5)&31,rm=(op>>16)&31,rd=op&31;
 uint64_t a=rn==31?0:(rn==29?s->fp:(rn==30?s->lr:s->x[rn]));
 uint64_t b=rm==31?s->sp:(rm==29?s->fp:(rm==30?s->lr:s->x[rm]));
 uint64_t v=expected_hash(a,b);
 if(rd==29)s->fp=v;else if(rd==30)s->lr=v;else if(rd<29)s->x[rd]=v;
 s->pc+=4;
}
static void pair_tests(void){
 uint32_t first=0x9aca3108,second=0x9ac83008,third=pacga_word(1,0,2);
 memcpy(memory+0x1004,&second,4);memcpy(memory+0x1008,&third,4);
 /* Missing/invalid pair opt-in remains off. Disabled development ABI cannot
  * select it. These init calls model distinct boot choices in the harness. */
 int old_boot=boot_flag;boot_flag=0;pair_flag=1;b2l_compat_init();assert(!b2l_compat_pair);boot_flag=old_boot;
 for(int flag=-1;flag<=2;flag++)if(flag!=1){pair_flag=flag;b2l_compat_init();assert(!b2l_compat_pair);}
 pair_flag=1;b2l_compat_init();assert(b2l_compat_pair);
 arm_saved_state_t s=initial(),want=s;expected_step(&want,first);expected_step(&want,second);
 unsigned queries=region_queries;uint64_t before=b2l_compat_perf[4];
 assert(b2l_arm64e_compat_emulate(&s,first));assert(!memcmp(&s,&want,sizeof(s)));
 assert(s.pc==0x1008&&region_queries==queries+3&&b2l_compat_perf[4]==before+1);
 /* Never consume a third instruction, even another PACGA. */
 assert(s.x[1]==want.x[1]);
 /* Debug state and single-step must keep the one-instruction boundary. */
 s=initial();s.cpsr|=PSR64_SS;want=s;expected_step(&want,first);
 assert(b2l_arm64e_compat_emulate(&s,first)&&!memcmp(&s,&want,sizeof(s)));
 host_thread.machine.DebugData=&host_thread;s=initial();want=s;expected_step(&want,first);
 assert(b2l_arm64e_compat_emulate(&s,first)&&!memcmp(&s,&want,sizeof(s)));host_thread.machine.DebugData=NULL;
 /* Warm pair-disabled path still commits exactly the first instruction. */
 pair_flag=0;b2l_compat_init();s=initial();want=s;expected_step(&want,first);
 assert(b2l_arm64e_compat_emulate(&s,first)&&!memcmp(&s,&want,sizeof(s)));
 pair_flag=1;b2l_compat_init();
 /* A second instruction's future execute failure preserves the first commit. */
 leaf_size=8;s=initial();want=s;expected_step(&want,first);
 assert(b2l_arm64e_compat_emulate(&s,first)&&!memcmp(&s,&want,sizeof(s)));leaf_size=0x3000;
 /* An opportunistic fetch fault also preserves exactly the first commit. */
 s=initial();want=s;expected_step(&want,first);atomic32_fail_at=atomic32_calls+3;
 assert(b2l_arm64e_compat_emulate(&s,first)&&!memcmp(&s,&want,sizeof(s)));atomic32_fail_at=0;
 /* A branch/non-PACGA following instruction must execute later in EL0. */
 uint32_t other=0xd65f0bff;memcpy(memory+0x1004,&other,4);s=initial();want=s;expected_step(&want,first);
 assert(b2l_arm64e_compat_emulate(&s,first)&&!memcmp(&s,&want,sizeof(s)));
 /* Full register alias coverage, SP/XZR semantics, and FP/LR destinations. */
 uint64_t rnd=UINT64_C(0x1827364554637281);
 for(unsigned i=0;i<2048;i++){
  rnd=rnd*UINT64_C(6364136223846793005)+1;
  first=pacga_word(rnd&31,(rnd>>5)&31,(rnd>>10)&31);
  second=pacga_word((rnd>>15)&31,(rnd>>20)&31,(rnd>>25)&31);
  memcpy(memory+0x1004,&second,4);s=initial();s.x[8]^=rnd;s.sp^=rnd&~UINT64_C(15);want=s;
  expected_step(&want,first);expected_step(&want,second);
  assert(b2l_arm64e_compat_emulate(&s,first)&&!memcmp(&s,&want,sizeof(s)));
 }
 memset(memory+0x1004,0,8);
 /* At least one of1025 calls is sampled, without adding per-call clocks. */
 uint64_t perf_before[6],perf_after[6];b2l_arm64e_perf_snapshot(perf_before);
 for(unsigned i=0;i<1025;i++){s=initial();assert(b2l_arm64e_compat_emulate(&s,pacga_word(0,1,2)));}
 b2l_arm64e_perf_snapshot(perf_after);assert(perf_after[0]>perf_before[0]);
 assert(perf_after[1]>perf_before[1]&&perf_after[2]>perf_before[2]&&perf_after[3]>perf_before[3]);
 assert(perf_after[1]>=perf_after[2]+perf_after[3]);
 puts("Bounded PACGA pair: CF chain, 2048 aliases, SP/XZR, FP/LR, two-instruction cap, faults, execute denial, disabled mode and sampled timing PASS");
}
int main(int argc,char **argv){
 assert(argc==2);bool use_siphash=!strcmp(argv[1],"siphash");assert(use_siphash||!strcmp(argv[1],"qarma"));
 /* Standard SipHash-2-4 known answer: key and 16-byte input 00..0f. */
 uint8_t kat[16];for(unsigned i=0;i<16;i++)kat[i]=i;SIPHASH_CTX katctx;
 assert(SipHashX(&katctx,2,4,kat,kat,16)==UINT64_C(0x3f2acc7f57c29bdb));

 arm_saved_state_t s=initial();
 boot_flag=0;b2l_compat_init();assert(!early_log_bytes);rejected_unchanged(s,0xd65f0bff);
 boot_flag=2;b2l_compat_init();assert(!early_log_bytes);rejected_unchanged(s,0xd65f0bff);

 /* Missing/zero/invalid algorithm opt-in all retain QARMA. Disabled runtime
  * cannot request SipHash. TUNABLES completes before the first callback. */
 uint64_t disabled_out=0;assert(b2l_compat_pacga(1,2,&disabled_out,NULL)==-1);
 boot_flag=0;siphash_flag=1;b2l_compat_init();assert(!b2l_compat_siphash&&!strcmp(b2l_arm64e_pacga_name(),"disabled"));
 boot_flag=1;siphash_flag=-1;b2l_compat_init();assert(!b2l_compat_siphash);
 siphash_flag=0;b2l_compat_init();assert(!b2l_compat_siphash);
 siphash_flag=2;b2l_compat_init();assert(!b2l_compat_siphash);
 siphash_flag=use_siphash?1:0;b2l_compat_init();
 assert(b2l_arm64e_compat_enabled()&&early_log_bytes&&b2l_compat_siphash==use_siphash);
 assert(!strcmp(b2l_arm64e_pacga_name(),use_siphash?"siphash24":"qarma5"));
 key_race_test();

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
 lookup_entry_missing=true;rejected_unchanged(s,0xd73f0910);lookup_entry_missing=false;
 iterator_error=5;rejected_unchanged(s,0xd73f0910);iterator_error=0;
 /* Child map has a different virtual base; use only parent-clipped bounds. */
 child_shift=0x400000;s=initial();s.x[8]=0x1800;
 assert(b2l_arm64e_compat_emulate(&s,0xd73f0910)&&s.pc==0x1800);child_shift=0;
 s=initial();s.x[8]=0x1800;

 leaf_start=0x1004;rejected_unchanged(s,0xd73f0910);leaf_start=0x1000;
 leaf_size=3;rejected_unchanged(s,0xd73f0910);leaf_size=0x3000;
 s.x[8]=0x3000;leaf_size=0x2000;rejected_unchanged(s,0xd73f0910);leaf_size=0x3000;
 s.x[8]=UINT64_C(0xfffffff007000000);rejected_unchanged(s,0xd73f0910);
 /* PACGA x0,x1,x2: persistent random software key, exact full result. */
 uint32_t pacga=0x9ac03000|(2u<<16)|(1u<<5);s=initial();assert(b2l_arm64e_compat_emulate(&s,pacga));uint64_t hash=s.x[0];assert(hash==expected_hash(s.x[1],s.x[2]));assert(!(hash&UINT32_MAX)&&entropy_calls==1);s=initial();assert(b2l_arm64e_compat_emulate(&s,pacga)&&s.x[0]==hash&&entropy_calls==1);
 uint64_t out=0;uint64_t value=UINT64_C(0x8877665544332211);memcpy(memory+0x2000,&value,8);
 assert(!b2l_compat_read_acquire(0x2000,8,&out,NULL)&&out==value);
 assert(!b2l_compat_read_acquire(0x2000,4,&out,NULL)&&out==0x44332211);
 assert(!b2l_compat_read_acquire(0x2002,2,&out,NULL)&&out==0x4433);
 assert(!b2l_compat_read_acquire(0x2003,1,&out,NULL)&&out==0x44);
 assert(b2l_compat_read_acquire(0x2001,2,&out,NULL));assert(b2l_compat_read_acquire(0x2000,0,&out,NULL));
 assert(!b2l_user_range(0,4)&&!b2l_user_range(UINT64_MAX-1,8)&&!b2l_user_range(MACH_VM_MAX_ADDRESS,4));
 copy_fault=true;assert(b2l_compat_read_acquire(0x2000,8,&out,NULL));copy_fault=false;
 pair_tests();
 assert(!map_locked&&map_locks==map_unlocks);
 puts("Direct execute lookup: exact4-byte locks, constant-submap descent, parent bounds, holes/errors, unlock-before-copyin and no stale permission reuse PASS");
 printf("B2L runtime %s: boundary, boot gate, 8-thread first-key race and mutex-free steady state PASS\n",b2l_arm64e_pacga_name());return 0;
}
