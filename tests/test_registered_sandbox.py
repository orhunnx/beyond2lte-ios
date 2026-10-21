#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the actual B2L provider, live MAC dispatch and privilege composition."""
import argparse,pathlib,re,subprocess,tempfile,shutil,os
p=argparse.ArgumentParser();p.add_argument('xnu',type=pathlib.Path);p.add_argument('overlay',type=pathlib.Path);a=p.parse_args()
sandbox=(a.overlay/'bsd/kern/b2l_sandbox.c').read_text();mac=(a.xnu/'security/mac_base.c').read_text();priv=(a.xnu/'bsd/kern/kern_priv.c').read_text()
def extract(text,name):
 m=re.search(r'(?m)^(?:static\s+)?int\s*\n'+re.escape(name)+r'\(',text);assert m,name
 start=text.index('{',m.end());depth=1
 for t in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|[{}]',text[start+1:],re.S):
  if t.group()=='{':depth+=1
  elif t.group()=='}':
   depth-=1
   if depth==0:return text[m.start():start+1+t.end()]
 raise AssertionError(name)
# Strip includes only: the new implementation/guards/functions remain verbatim.
sandbox=re.sub(r'^#include[^\n]*\n','',sandbox,flags=re.M)
legacy=extract(mac,'b2l_sandbox_container_expected').replace('b2l_sandbox_container_expected','legacy_container_expected')
dispatch=extract(mac,'__mac_syscall').replace('b2l_sandbox_container_expected','legacy_container_expected')
body=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <pthread.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define CONFIG_MACF 1
#define ENOPOLICY 103
#define PRIV_WORK_INTERVAL 1008
#define PRIVCHECK_DEFAULT_UNPRIVILEGED_FLAG 1
#define MAC_MAX_POLICY_NAME 64
#define AUDIT_ARG(...) ((void)0)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define os_atomic_or_orig(p,v,m) atomic_fetch_or_explicit((p),(v),memory_order_##m)
#define os_atomic_xchg(p,v,m) atomic_exchange_explicit((p),(v),memory_order_##m)
typedef unsigned u_int,mac_policy_handle_t;
typedef uint64_t user_addr_t,user64_addr_t;
struct proc {bool is64;};typedef struct proc *proc_t;
struct cred {unsigned uid;};typedef struct cred *kauth_cred_t;
struct mac_policy_ops {int (*mpo_policy_syscall)(struct proc *,int,user_addr_t);int(*mpo_priv_grant)(kauth_cred_t,int);};
struct mac_policy_conf {const char *mpc_name,*mpc_fullname;const struct mac_policy_ops *mpc_ops;};
struct __mac_syscall_args {user_addr_t policy;int call;user_addr_t arg;};
static struct {struct {struct mac_policy_conf *mpc;}entries[2];u_int staticmax,maxindex;}mac_policy_list;
static bool compat,enforcement,dynamic;
static unsigned registrations,copyins,copyouts,unbusy_calls;
static int registration_error,dispatch_check_error,priv_restrict_error,fail_copyout;
static _Atomic unsigned log_lines;
static uint8_t memory[0x4000];
static int test_printf(const char *format,...) {(void)format;atomic_fetch_add(&log_lines,1);return 0;}
#define printf test_printf
static bool b2l_arm64e_compat_enabled(void){return compat;}
static bool cs_process_global_enforcement(void){return enforcement;}
static bool proc_is64bit(proc_t p){return p->is64;}
static int copyin(user_addr_t a,void *p,size_t n){copyins++;if(a<0x1000||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;memcpy(p,memory+a,n);return 0;}
static int copyout(const void *p,user_addr_t a,size_t n){copyouts++;if((int)copyouts==fail_copyout||a<0x1000||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;memcpy(memory+a,p,n);return 0;}
static int copyinstr(user_addr_t a,char *p,size_t n,size_t *z){for(size_t i=0;i<n;i++){if(copyin(a+i,p+i,1))return EFAULT;if(!p[i]){*z=i+1;return 0;}}return ENAMETOOLONG;}
static int mac_proc_check_mac_syscall(proc_t p,const char *s,int c){(void)p;(void)s;(void)c;return dispatch_check_error;}
static int mac_policy_list_conditional_busy(void){return dynamic;}
static void mac_policy_list_unbusy(void){unbusy_calls++;}
static int mac_policy_register(struct mac_policy_conf *c,mac_policy_handle_t *h,void *unused){
 (void)unused;registrations++;if(registration_error)return registration_error;
 if(mac_policy_list.entries[0].mpc)return EEXIST;
 assert(!strcmp(c->mpc_name,"Sandbox")&&c->mpc_fullname&&c->mpc_ops->mpo_policy_syscall&&c->mpc_ops->mpo_priv_grant);
 *h=0;mac_policy_list.entries[0].mpc=c;mac_policy_list.staticmax=1;return 0;
}
'''+sandbox+legacy+dispatch+r'''
static int mac_priv_check(kauth_cred_t c,int p){(void)c;(void)p;return priv_restrict_error;}
static int mac_priv_grant(kauth_cred_t c,int p){
 struct mac_policy_conf *m=mac_policy_list.entries[0].mpc;
 return m&&m->mpc_ops->mpo_priv_grant?m->mpc_ops->mpo_priv_grant(c,p):EPERM;
}
static unsigned kauth_cred_getuid(kauth_cred_t c){return c->uid;}
'''+extract(priv,'priv_check_cred')+r'''
static struct proc process={true};
static struct __mac_syscall_args args={0x1000,0x43,0x1100};
static void memory_setup(void){
 memset(memory,0,sizeof(memory));strcpy((char *)memory+0x1000,"Sandbox");
 uint64_t outputs[]={0x2000,0x2100};memcpy(memory+0x1100,outputs,16);
 memset(memory+0x2000,0xaa,8);memset(memory+0x2100,0xbb,8);
 copyins=copyouts=0;fail_copyout=0;args.call=0x43;args.arg=0x1100;
}
static void reset(void){
 memset(&mac_policy_list,0,sizeof(mac_policy_list));compat=true;enforcement=dynamic=false;process.is64=true;
 registrations=unbusy_calls=0;registration_error=dispatch_check_error=priv_restrict_error=0;
 b2l_sandbox_registered=false;atomic_store(&b2l_sandbox_seen[0],0);atomic_store(&b2l_sandbox_seen[1],0);atomic_store(&b2l_sandbox_other_seen,0);
 atomic_store(&b2l_sandbox_priv_seen,0);atomic_store(&b2l_sandbox_other_priv_seen,0);atomic_store(&log_lines,0);memory_setup();
}
static uint64_t get(unsigned a){uint64_t v;memcpy(&v,memory+a,8);return v;}
static int syscall(void){return __mac_syscall(&process,&args,NULL);}
static int other_policy(proc_t p,int c,user_addr_t a){(void)p;(void)c;(void)a;return EACCES;}
static void test_registration_and_gates(void){
 reset();compat=false;b2l_sandbox_register();assert(!registrations&&!atomic_load(&log_lines));
 compat=true;enforcement=true;b2l_sandbox_register();assert(!registrations);
 enforcement=false;b2l_sandbox_register();assert(registrations==1&&b2l_sandbox_registered);
 b2l_sandbox_register();assert(registrations==1);assert(!syscall()&&!get(0x2000)&&!get(0x2100));
 memory_setup();compat=false;assert(syscall()==ENOPOLICY&&!copyouts);assert(b2l_sandbox_priv_grant(NULL,1008)==EPERM);
 compat=true;enforcement=true;assert(syscall()==ENOPOLICY&&!copyouts);assert(b2l_sandbox_priv_grant(NULL,1008)==EPERM);
 enforcement=false;process.is64=false;assert(syscall()==ENOPOLICY&&!copyouts);process.is64=true;
 dispatch_check_error=EACCES;assert(syscall()==EACCES&&!copyouts);
 reset();registration_error=ENOMEM;b2l_sandbox_register();assert(registrations==1&&!b2l_sandbox_registered&&!mac_policy_list.entries[0].mpc);
 registration_error=0;b2l_sandbox_register();assert(registrations==2&&b2l_sandbox_registered);
 /* Existing real policy must not be replaced or overridden by either shim. */
 reset();const struct mac_policy_ops real_ops={other_policy,NULL};struct mac_policy_conf real={"Sandbox","real",&real_ops};mac_policy_list.entries[0].mpc=&real;mac_policy_list.staticmax=1;
 b2l_sandbox_register();assert(!b2l_sandbox_registered&&mac_policy_list.entries[0].mpc==&real);assert(syscall()==EACCES&&!copyouts);
}
static void test_operations_and_copyio(void){
 reset();b2l_sandbox_register();
 for(int op=0;op<128;op++){memory_setup();args.call=op;int e=syscall();assert(e==((op>=4&&op<=6)?ENOENT:0));if(op!=67)assert(!copyouts);}
 memory_setup();args.call=-1;assert(!syscall()&&!copyouts);args.call=100000;assert(!syscall());
 memory_setup();strcpy((char *)memory+0x1000,"Other");assert(syscall()==ENOPOLICY&&!copyouts);
 memory_setup();args.arg=1;assert(syscall()==EFAULT&&!copyouts);
 memory_setup();fail_copyout=1;assert(syscall()==EFAULT&&copyouts==1&&get(0x2000)==UINT64_C(0xaaaaaaaaaaaaaaaa));
 memory_setup();fail_copyout=2;assert(syscall()==EFAULT&&copyouts==2&&!get(0x2000)&&get(0x2100)==UINT64_C(0xbbbbbbbbbbbbbbbb));
 memory_setup();uint64_t zeros[2]={0,0};memcpy(memory+0x1100,zeros,16);assert(!syscall()&&!copyouts);
 memory_setup();uint64_t alias[2]={0x1100,0x2100};memcpy(memory+0x1100,alias,16);assert(!syscall()&&copyouts==2&&!get(0x1100)&&!get(0x2100));
 memory_setup();uint64_t same[2]={0x2000,0x2000};memcpy(memory+0x1100,same,16);assert(!syscall()&&copyouts==2&&!get(0x2000));
 memory_setup();uint64_t invalid[2]={UINT64_MAX,0x2100};memcpy(memory+0x1100,invalid,16);assert(syscall()==EFAULT&&copyouts==1&&get(0x2100)==UINT64_C(0xbbbbbbbbbbbbbbbb));
 /* Dynamic provider path uses normal list unbusy and the same real callback. */
 reset();b2l_sandbox_register();mac_policy_list.staticmax=0;dynamic=true;assert(!syscall()&&unbusy_calls==1&&copyouts==2);
}
static void test_privilege_composition(void){
 reset();b2l_sandbox_register();struct cred regular={501},root={0};
 assert(!priv_check_cred(&regular,PRIV_WORK_INTERVAL,0));
 assert(priv_check_cred(&regular,1009,0)==EPERM);
 assert(!priv_check_cred(&root,1009,0));
 assert(priv_check_cred(&root,1009,PRIVCHECK_DEFAULT_UNPRIVILEGED_FLAG)==EPERM);
 priv_restrict_error=EACCES;assert(priv_check_cred(&regular,PRIV_WORK_INTERVAL,0)==EACCES);priv_restrict_error=0;
 enforcement=true;assert(priv_check_cred(&regular,PRIV_WORK_INTERVAL,0)==EPERM);
}
static void *concurrent(void *arg){(void)arg;for(unsigned i=0;i<5000;i++){assert(!b2l_sandbox_syscall(&process,33,0));assert(!b2l_sandbox_priv_grant(NULL,1008));}return NULL;}
static void test_bounded_atomic_logs(void){
 reset();b2l_sandbox_register();atomic_store(&log_lines,0);pthread_t threads[16];
 for(unsigned i=0;i<16;i++)assert(!pthread_create(&threads[i],NULL,concurrent,NULL));
 for(unsigned i=0;i<16;i++)assert(!pthread_join(threads[i],NULL));
 assert(atomic_load(&log_lines)==2);
 for(int op=-100;op<1000;op++)b2l_sandbox_log_operation(op);
 for(int priv=-100;priv<2000;priv++)b2l_sandbox_priv_grant(NULL,priv);
 assert(atomic_load(&log_lines)==129+65);
}
int main(void){test_registration_and_gates();test_operations_and_copyio();test_privilege_composition();test_bounded_atomic_logs();puts("Registered Sandbox: actual source+MAC dispatch+privilege composition, copyio failures, real-policy precedence, gates and concurrent bounded logging passed");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='b2l-registered-sandbox-') as td:
 d=pathlib.Path(td);(d/'test.c').write_text(body)
 compiler=os.getenv('CLANG') or shutil.which('clang');assert compiler,'clang required'
 subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
