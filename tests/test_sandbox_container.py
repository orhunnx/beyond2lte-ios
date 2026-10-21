#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Test actual MAC syscall dispatch and B2L container-query copyio semantics."""
import pathlib,re,subprocess,sys,tempfile
x=pathlib.Path(sys.argv[1]);s=(x/'security/mac_base.c').read_text()
def extract(name):
 m=re.search(r'(?m)^'+re.escape(name)+r'\(',s);assert m,name
 start=s.rfind('\nstatic int\n',0,m.start()) if name.startswith('b2l_') else s.rfind('\nint\n',0,m.start())
 begin=s.index('{',m.end());depth=1
 for t in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|[{}]',s[begin+1:],re.S):
  if t.group()=='{':depth+=1
  elif t.group()=='}':
   depth-=1
   if not depth:return s[start:begin+1+t.end()+1]
 raise AssertionError(name)
h=r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define ENOPOLICY 103
#define MAC_MAX_POLICY_NAME 64
#define AUDIT_ARG(...) ((void)0)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
typedef unsigned u_int;
typedef uint64_t user_addr_t,user64_addr_t;
struct proc{bool is64;};typedef struct proc *proc_t;
struct __mac_syscall_args{user_addr_t policy;int call;user_addr_t arg;};
struct mac_policy_ops{int(*mpo_policy_syscall)(proc_t,int,user_addr_t);};
struct mac_policy_conf{const char *mpc_name;struct mac_policy_ops *mpc_ops;};
static struct {struct {struct mac_policy_conf *mpc;}entries[2];u_int staticmax,maxindex;}mac_policy_list;
static unsigned unbusy_calls,copyouts,policy_calls;
static int check_error,provider_error,fail_copyout;
static bool dynamic,compat=true,enforcement;
static uint8_t memory[0x4000];
static int copyin(user_addr_t a,void*p,size_t n){if(a<0x1000||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;memcpy(p,memory+a,n);return 0;}
static int copyout(const void*p,user_addr_t a,size_t n){++copyouts;if((int)copyouts==fail_copyout||a<0x1000||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;memcpy(memory+a,p,n);return 0;}
static int copyinstr(user_addr_t a,char*p,size_t n,size_t*z){for(size_t i=0;i<n;i++){if(copyin(a+i,p+i,1))return EFAULT;if(!p[i]){*z=i+1;return 0;}}return ENAMETOOLONG;}
static int mac_proc_check_mac_syscall(proc_t p,const char*t,int c){(void)p;(void)t;(void)c;return check_error;}
static int mac_policy_list_conditional_busy(void){return dynamic;}
static void mac_policy_list_unbusy(void){++unbusy_calls;}
static bool b2l_arm64e_compat_enabled(void){return compat;}
static int cs_process_global_enforcement(void){return enforcement;}
static bool proc_is64bit(proc_t p){return p->is64;}
static int provider(proc_t p,int c,user_addr_t a){(void)p;(void)c;(void)a;++policy_calls;return provider_error;}
"""+extract('b2l_sandbox_container_expected')+extract('__mac_syscall')+r"""
static uint64_t get(unsigned a){uint64_t v;memcpy(&v,memory+a,8);return v;}
static void setup(void){
 memset(memory,0,sizeof(memory));strcpy((char*)memory+0x1000,"Sandbox");
 uint64_t args[]={0x2000,0x2100};memcpy(memory+0x1100,args,sizeof(args));
 memset(memory+0x2000,0xaa,8);memset(memory+0x2100,0xbb,8);copyouts=0;fail_copyout=0;
}
int main(void){
 struct proc p={true};struct __mac_syscall_args a={0x1000,0x43,0x1100};
 setup();assert(!__mac_syscall(&p,&a,0)&&!get(0x2000)&&!get(0x2100)&&copyouts==2);
 setup();compat=false;assert(__mac_syscall(&p,&a,0)==ENOPOLICY&&!copyouts);compat=true;
 setup();enforcement=true;assert(__mac_syscall(&p,&a,0)==ENOPOLICY&&!copyouts);enforcement=false;
 setup();p.is64=false;assert(__mac_syscall(&p,&a,0)==ENOPOLICY&&!copyouts);p.is64=true;
 setup();a.call=1;assert(__mac_syscall(&p,&a,0)==ENOPOLICY&&!copyouts);a.call=0x43;
 setup();strcpy((char*)memory+0x1000,"Other");assert(__mac_syscall(&p,&a,0)==ENOPOLICY&&!copyouts);
 setup();check_error=EPERM;assert(__mac_syscall(&p,&a,0)==EPERM&&!copyouts);check_error=0;
 setup();a.arg=1;assert(__mac_syscall(&p,&a,0)==EFAULT&&!copyouts);a.arg=0x1100;
 setup();fail_copyout=1;assert(__mac_syscall(&p,&a,0)==EFAULT&&copyouts==1&&get(0x2000)==UINT64_C(0xaaaaaaaaaaaaaaaa));
 setup();fail_copyout=2;assert(__mac_syscall(&p,&a,0)==EFAULT&&copyouts==2&&!get(0x2000)&&get(0x2100)==UINT64_C(0xbbbbbbbbbbbbbbbb));
 setup();uint64_t no_outputs[2]={0};memcpy(memory+0x1100,no_outputs,16);assert(!__mac_syscall(&p,&a,0)&&!copyouts);
 struct mac_policy_ops ops={provider};struct mac_policy_conf conf={"Sandbox",&ops};
 mac_policy_list.entries[0].mpc=&conf;mac_policy_list.staticmax=1;
 setup();provider_error=ENOPOLICY;assert(__mac_syscall(&p,&a,0)==ENOPOLICY&&!copyouts&&policy_calls==1);
 setup();provider_error=EPERM;assert(__mac_syscall(&p,&a,0)==EPERM&&!copyouts&&policy_calls==2);
 setup();provider_error=0;assert(!__mac_syscall(&p,&a,0)&&!copyouts&&policy_calls==3);
 mac_policy_list.staticmax=0;dynamic=true;setup();provider_error=ENOPOLICY;
 assert(__mac_syscall(&p,&a,0)==ENOPOLICY&&!copyouts&&policy_calls==4&&unbusy_calls==1);
 puts("Actual MAC dispatch: container outputs, copyio failures, opt-in gates, unknown operations and real-policy precedence passed");
}
"""
with tempfile.TemporaryDirectory(prefix='b2l-container-test-') as td:
 p=pathlib.Path(td);(p/'test.c').write_text(h)
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run(['xcrun','clang','-isysroot',sdk,'-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
