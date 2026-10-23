#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile and exercise the actual bounded B2L extension registry under ASan/UBSan."""
import pathlib, tempfile, subprocess, shutil, os
root=pathlib.Path(__file__).resolve().parents[1]
header=root/'xnu-overlay/bsd/kern/b2l_sandbox_extensions.h'
body=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdarg.h>
#define ENOPOLICY 103
#define LCK_GRP_DECLARE(a,b) int a __attribute__((unused))
#define LCK_SPIN_DECLARE(a,b) pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned lock_depth;
static void lck_spin_lock(pthread_mutex_t*p){assert(!lock_depth);assert(!pthread_mutex_lock(p));lock_depth++;}
static void lck_spin_unlock(pthread_mutex_t*p){assert(lock_depth==1);lock_depth--;assert(!pthread_mutex_unlock(p));}
typedef uint64_t user_addr_t;
struct proc {uint64_t unique;int pid;unsigned version;bool is64;};
typedef struct proc *proc_t;
static struct proc processes[12];
static proc_t proc_find(int pid){assert(!lock_depth);for(unsigned i=0;i<12;i++)if(processes[i].pid==pid)return &processes[i];return NULL;}
static void proc_rele(proc_t p){assert(!lock_depth&&p);}
static int proc_pid(proc_t p){assert(!lock_depth&&p);return p->pid;}
static char diag_output[32768];static size_t diag_used;
static pthread_mutex_t diag_mutex=PTHREAD_MUTEX_INITIALIZER;
static void b2l_diag_record(const char*format,...){assert(!lock_depth);pthread_mutex_lock(&diag_mutex);va_list ap;va_start(ap,format);int n=vsnprintf(diag_output+diag_used,sizeof(diag_output)-diag_used,format,ap);va_end(ap);assert(n>0&&(size_t)n<sizeof(diag_output)-diag_used);diag_used+=(size_t)n;pthread_mutex_unlock(&diag_mutex);}
static uint64_t proc_uniqueid(proc_t p){assert(!lock_depth&&p);return p->unique;}
static unsigned proc_pidversion(proc_t p){assert(!lock_depth&&p);return p->version;}
static bool proc_is64bit(proc_t p){return p->is64;}
static bool active=true;
static bool b2l_sandbox_development_enabled(void){return active;}
static _Thread_local unsigned char memory[0x10000];
static _Thread_local bool fail_write;
static _Thread_local void (*on_copyout)(void);
static int copyin(user_addr_t a,void*p,size_t n){assert(!lock_depth);if(a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;memcpy(p,memory+a,n);return 0;}
static int copyout(const void*p,user_addr_t a,size_t n){assert(!lock_depth);if(on_copyout){void(*fn)(void)=on_copyout;on_copyout=NULL;fn();}if(fail_write||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;memcpy(memory+a,p,n);return 0;}
static int copyinstr(user_addr_t a,void*p,size_t n,size_t*done){assert(!lock_depth);for(size_t i=0;i<n;i++){if(a+i>=sizeof(memory))return EFAULT;((char*)p)[i]=memory[a+i];if(!memory[a+i]){*done=i+1;return 0;}}return ENAMETOOLONG;}
static atomic_uint_fast64_t random_serial=1;
static void read_random(void*p,size_t n){assert(!lock_depth);uint64_t x=atomic_fetch_add(&random_serial,1);for(size_t i=0;i<n;i++)((uint8_t*)p)[i]=(uint8_t)((x>>((i%8)*8))^(i*37));}
'''+f'\n#include "{header}"\n'+r'''
static struct b2l_ext_issue_args issueargs;
static void putargs(const void*a,size_t n){memcpy(memory+0x1000,a,n);}
static void setup(void){memset(memory,0,sizeof(memory));fail_write=false;strcpy((char*)memory+0x2000,"com.apple.app-sandbox.read-write");strcpy((char*)memory+0x3000,"/private/var/mobile/Library/Test");issueargs=(struct b2l_ext_issue_args){0x2000,0,0x3000,0,0x4000,0,0};}
static int issue(proc_t p){putargs(&issueargs,sizeof(issueargs));return b2l_extension_syscall(p,5,0x1000);}
static int consume(proc_t p,uint64_t*out){struct b2l_ext_consume_args a={0x4000,73,0x5000};putargs(&a,sizeof(a));int e=b2l_extension_syscall(p,6,0x1000);if(!e)memcpy(out,memory+0x5000,8);return e;}
static int release(proc_t p,uint64_t handle){struct b2l_ext_release_args a={handle,0,0};putargs(&a,sizeof(a));return b2l_extension_syscall(p,7,0x1000);}
static void reset_registry(void){memset(b2l_ext_tokens,0,sizeof(b2l_ext_tokens));memset(b2l_ext_handles,0,sizeof(b2l_ext_handles));}
static unsigned token_count(void){unsigned n=0;for(unsigned i=0;i<B2L_EXT_TOKENS;i++)n+=b2l_ext_tokens[i].used;return n;}
static unsigned handle_count(void){unsigned n=0;for(unsigned i=0;i<B2L_EXT_HANDLES;i++)n+=b2l_ext_handles[i].used;return n;}
static void test_basic(void){
 reset_registry();setup();assert(!issue(&processes[0]));assert(token_count()==1);assert(!memcmp(memory+0x4000,"b2l-dev-",8));uint64_t h,h2;
 assert(!consume(&processes[1],&h)&&h>0&&h<=INT64_MAX);assert(!consume(&processes[1],&h2)&&h2!=h);assert(handle_count()==2);
 assert(release(&processes[2],h)==ENOENT&&handle_count()==2);assert(!release(&processes[1],h));assert(release(&processes[1],h)==ENOENT);
 assert(!release(&processes[1],h2));assert(!token_count()&&!handle_count());assert(consume(&processes[1],&h)==ENOENT);
}
static void test_native_container_classes(void){
 const char*names[]={"com.apple.sandbox.container","com.apple.sandbox.application-group","com.apple.sandbox.system-container","com.apple.sandbox.system-group"};
 for(unsigned i=0;i<4;i++){
  reset_registry();setup();strcpy((char*)memory+0x2000,names[i]);
  strcpy((char*)memory+0x3000,"/private/var/mobile/Containers/Shared/AppGroup/FIXTURE");
  issueargs.flags=0x30000;issueargs.target_pid=processes[1].pid;issueargs.target_version=processes[1].version;
  assert(!issue(&processes[0]));assert(token_count()==1);
  assert(b2l_ext_tokens[0].class_id==i+3&&b2l_ext_tokens[0].writable);
  uint64_t handle;assert(consume(&processes[2],&handle)==EPERM);
  assert(!consume(&processes[1],&handle));assert(!release(&processes[1],handle));
  assert(!token_count()&&!handle_count());
 }
 setup();strcpy((char*)memory+0x2000,"com.apple.sandbox.application-group.extra");assert(issue(&processes[0])==ENOTSUP);
 setup();strcpy((char*)memory+0x2000,"com.apple.sandbox.");assert(issue(&processes[0])==ENOTSUP);
 setup();strcpy((char*)memory+0x2000,"com.apple.app-sandbox.read");assert(!issue(&processes[0]));assert(b2l_ext_tokens[0].class_id==1&&!b2l_ext_tokens[0].writable);
 b2l_extension_process_exit(&processes[0]);
}
static void test_copy_rollback(void){
 reset_registry();setup();fail_write=true;assert(issue(&processes[0])==EFAULT&&!token_count());fail_write=false;assert(!issue(&processes[0]));
 uint64_t h;fail_write=true;assert(consume(&processes[1],&h)==EFAULT&&token_count()==1&&!handle_count());fail_write=false;
 assert(!consume(&processes[1],&h));assert(!release(&processes[1],h));
 setup();issueargs.token=UINT64_MAX;assert(issue(&processes[0])==EFAULT&&!token_count());
 assert(b2l_extension_syscall(&processes[0],5,UINT64_MAX)==EFAULT);
}
static void test_bindings_and_exit(void){
 reset_registry();setup();issueargs.flags=0x30000;issueargs.target_pid=processes[1].pid;issueargs.target_version=processes[1].version+1;assert(issue(&processes[0])==ESRCH);
 issueargs.target_version--;assert(!issue(&processes[0]));uint64_t h;assert(consume(&processes[2],&h)==EPERM);assert(!consume(&processes[1],&h));
 b2l_extension_process_exit(&processes[1]);assert(!token_count()&&!handle_count());assert(release(&processes[1],h)==ENOENT);
 setup();assert(!issue(&processes[0]));b2l_extension_process_exit(&processes[0]);assert(!token_count());
 setup();assert(!issue(&processes[0]));assert(!consume(&processes[1],&h));b2l_extension_process_exit(&processes[0]);assert(token_count()==1);b2l_extension_process_exit(&processes[1]);assert(!token_count()&&!handle_count());
 setup();issueargs.flags=0x10000;issueargs.target_pid=processes[1].pid;assert(!issue(&processes[0]));processes[1].unique++;
 assert(consume(&processes[1],&h)==EPERM);processes[1].unique--;b2l_extension_process_exit(&processes[1]);
}
static uint64_t release_during_copy_handle;
static void release_during_copy(void){assert(!release(&processes[1],release_during_copy_handle));}
static void exit_target_during_copy(void){b2l_extension_process_exit(&processes[1]);}
static void test_publication_races(void){
 reset_registry();setup();assert(!issue(&processes[0]));uint64_t h;
 assert(!consume(&processes[1],&release_during_copy_handle));
 on_copyout=release_during_copy;fail_write=true;
 assert(consume(&processes[1],&h)==EFAULT);fail_write=false;
 assert(!token_count()&&!handle_count());
 setup();issueargs.flags=0x30000;issueargs.target_pid=processes[1].pid;issueargs.target_version=processes[1].version;
 on_copyout=exit_target_during_copy;assert(issue(&processes[0])==ESRCH);
 assert(!token_count()&&!handle_count());
}
static void test_validation(void){
 reset_registry();const char*bad[]={"relative","/","/private//var","/private/./var","/private/../var","/private/var/","/private/var\n"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){setup();strcpy((char*)memory+0x3000,bad[i]);assert(issue(&processes[0])==EINVAL);}
 setup();issueargs.type=1;assert(issue(&processes[0])==ENOTSUP);setup();strcpy((char*)memory+0x2000,"forged.class");assert(issue(&processes[0])==ENOTSUP);
 setup();issueargs.flags=0x20000;assert(issue(&processes[0])==EINVAL);setup();issueargs.flags=0x40000;assert(issue(&processes[0])==EINVAL);
 setup();memset(memory+0x3000,'x',1024);assert(issue(&processes[0])==ENAMETOOLONG);
 setup();assert(!issue(&processes[0]));uint64_t h;memory[0x4009]='X';assert(consume(&processes[1],&h)==EINVAL);
 memory[0x4009]='a';assert(consume(&processes[1],&h)==ENOENT);struct b2l_ext_consume_args a={0x4000,UINT64_MAX,0x5000};putargs(&a,sizeof(a));assert(b2l_extension_syscall(&processes[1],6,0x1000)==EINVAL);
 reset_registry();setup();active=false;assert(issue(&processes[0])==ENOPOLICY);active=true;processes[0].is64=false;assert(issue(&processes[0])==ENOPOLICY);processes[0].is64=true;
 assert(b2l_extension_syscall(NULL,5,0x1000)==ENOPOLICY);assert(b2l_extension_syscall(&processes[0],99,0)==ENOTSUP);
}
static void test_capacity_and_path_release(void){
 reset_registry();setup();for(unsigned i=0;i<B2L_EXT_TOKENS;i++)assert(!issue(&processes[0]));assert(issue(&processes[0])==ENOSPC);b2l_extension_process_exit(&processes[0]);assert(!token_count());
 setup();assert(!issue(&processes[0]));uint64_t h;for(unsigned i=0;i<B2L_EXT_HANDLES;i++)assert(!consume(&processes[1],&h));assert(consume(&processes[1],&h)==ENOSPC);
 struct b2l_ext_release_args a={0,0,0x3000};putargs(&a,sizeof(a));assert(!b2l_extension_syscall(&processes[1],7,0x1000));assert(!handle_count()&&!token_count());
 struct b2l_ext_last_args last={1,0,0,0x6000};putargs(&last,sizeof(last));assert(b2l_extension_syscall(&processes[1],0x50,0x1000)==ENOTSUP);
}
static void *worker(void*arg){unsigned i=(unsigned)(uintptr_t)arg;for(unsigned n=0;n<300;n++){setup();struct b2l_ext_issue_args a=issueargs;putargs(&a,sizeof(a));assert(!b2l_extension_syscall(&processes[i],5,0x1000));uint64_t h;assert(!consume(&processes[i],&h));assert(!release(&processes[i],h));}return NULL;}
static void test_concurrency(void){reset_registry();pthread_t ts[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&ts[i],NULL,worker,(void*)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(ts[i],NULL));assert(!token_count()&&!handle_count());}
int main(void){for(unsigned i=0;i<12;i++)processes[i]=(struct proc){10000+i,100+(int)i,20+i,true};test_native_container_classes();test_basic();test_copy_rollback();test_bindings_and_exit();test_publication_races();test_validation();test_capacity_and_path_release();test_concurrency();assert(!strstr(diag_output,"b2l-dev-"));assert(diag_used<8192);puts("B2L extension actual registry: native container classes, exact-name rejection, ABI, transfer/owner/refcount, copies/rollback, forged/stale/bound tokens, malformed input, capacity, exit cleanup and 8-thread concurrency passed");}
'''
with tempfile.TemporaryDirectory(prefix='b2l-ext-test-') as td:
    d=pathlib.Path(td);(d/'test.c').write_text(body)
    compiler=os.getenv('CLANG') or shutil.which('clang');assert compiler
    subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
