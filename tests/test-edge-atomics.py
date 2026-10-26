#!/usr/bin/env python3
"""Exercise actual Edge registry fallback and state methods under contention."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'out/build-env-0063/src/xnu/osfmk/kern/sched_clutch.c').read_text()
a=s.index('#if defined(B2L)\n/* 8 CPUs need only16 bits;');b=s.index('#if defined(B2L) && OS_ATOMIC_USE_LLSC',a);registry=s[a:b]
a=s.index('#define SCHED_STP_RMW_LOOP(p, ov, nv, m, ...)');b=s.index('\n#else\n#if OS_ATOMIC_USE_LLSC',a);macro=s[a:b]
a=s.index('void\nsched_edge_stir_the_pot_clear_registry_entry(void)');b=s.index('/* Stir-the-pot is designed',a);methods=s[a:b]
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#define B2L 1
#define MAX_CPUS 8
#define static_assert _Static_assert
#undef __enum_decl
#define __enum_decl(name,type,...) typedef enum __VA_ARGS__ name
#define bits_mask(n) ((UINT64_C(1)<<(n))-1)
#define KDBG(...) ((void)0)
#define os_atomic_load(p,m) atomic_load_explicit((p),memory_order_relaxed)
static unsigned inject;
static int compare(_Atomic uint64_t*p,uint64_t expected,uint64_t desired,uint64_t*got){
 if(inject){inject=0;atomic_fetch_or_explicit(p,UINT64_C(1)<<14,memory_order_relaxed);}
 uint64_t old=expected;int result=atomic_compare_exchange_strong_explicit(p,&old,desired,memory_order_relaxed,memory_order_relaxed);*got=old;
 return result;
}
#define os_atomic_cmpxchgv(p,e,v,g,m) compare((p),(e),(v),(g))
static _Thread_local struct {int cpu_id;} processor;
#define current_processor() (&processor)
'''
post=r'''
static void *writer(void *arg){
 processor.cpu_id=(int)(uintptr_t)arg;
 for(unsigned n=0;n<20000;++n){sched_edge_stir_the_pot_set_registry_entry(false);sched_edge_stir_the_pot_clear_registry_entry();}
 sched_edge_stir_the_pot_set_registry_entry(false);return 0;
}
int main(void){
 assert(sizeof(sched_edge_stp_registry_t)==8);
 for(unsigned cpu=0;cpu<8;++cpu)for(unsigned state=0;state<4;++state){
  processor.cpu_id=cpu;uint64_t original=SESTP_SET_STATE(UINT64_C(0xa5a5dead00000000),cpu,state);
  atomic_store(&sched_edge_stir_the_pot_global_registry,original);sched_edge_stir_the_pot_set_registry_entry(false);
  uint64_t expected=state>=2?original:SESTP_SET_STATE(original,cpu,1);assert(atomic_load(&sched_edge_stir_the_pot_global_registry)==expected);
  sched_edge_stir_the_pot_set_registry_entry(true);assert(atomic_load(&sched_edge_stir_the_pot_global_registry)==SESTP_SET_STATE(original,cpu,1));
  sched_edge_stir_the_pot_clear_registry_entry();assert(atomic_load(&sched_edge_stir_the_pot_global_registry)==SESTP_SET_STATE(original,cpu,0));
  sched_edge_stir_the_pot_clear_registry_entry();assert(atomic_load(&sched_edge_stir_the_pot_global_registry)==SESTP_SET_STATE(original,cpu,0));
 }
 // A competing CPU changes another slot between candidate calculation and CAS.
 processor.cpu_id=0;atomic_store(&sched_edge_stir_the_pot_global_registry,0);inject=1;sched_edge_stir_the_pot_set_registry_entry(false);
 assert(atomic_load(&sched_edge_stir_the_pot_global_registry)==((UINT64_C(1)<<14)|1));
 atomic_store(&sched_edge_stir_the_pot_global_registry,UINT64_C(0xa5a5dead00000000));
 pthread_t t[8];for(unsigned i=0;i<8;++i)assert(!pthread_create(t+i,0,writer,(void*)(uintptr_t)i));
 for(unsigned i=0;i<8;++i)assert(!pthread_join(t[i],0));
 assert(atomic_load(&sched_edge_stir_the_pot_global_registry)==UINT64_C(0xa5a5dead00005555));
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-edge-atomics-') as td:
 p=Path(td);(p/'test.c').write_text(pre+registry+macro+'\n'+methods+post)
 subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=30)
print('Actual Edge 64-bit CAS/state methods:all8 slots/all4 states, abort/override, forced CAS retry,8 concurrent CPUs/no lost neighboring updates PASS')
