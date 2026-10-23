#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Actual unsupported AMFI callback: ABI, copy faults, gates and policy precedence."""
import os, pathlib, shutil, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
provider=root/"xnu-overlay/bsd/kern/b2l_amfi_constraint.h"
body=r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define ENOPOLICY 103
#ifndef __unused
#define __unused __attribute__((unused))
#endif
typedef uint64_t user_addr_t,user64_addr_t;
typedef unsigned mac_policy_handle_t;
struct proc {bool is64;};
struct mac_policy_ops {int (*mpo_policy_syscall)(struct proc*,int,user_addr_t);};
struct mac_policy_conf {const char *mpc_name,*mpc_fullname;const struct mac_policy_ops *mpc_ops;};
static bool active=true;
static bool b2l_amfi_active(void){return active;}
static bool proc_is64bit(struct proc*p){return p&&p->is64;}
static uint8_t memory[0x10000];
static unsigned reads,writes,fail_read;
static bool fail_write;
static int copyin(user_addr_t a,void *p,size_t n){
 reads++;if(reads==fail_read||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;
 memcpy(p,memory+a,n);return 0;
}
static int copyout(const void *p,user_addr_t a,size_t n){
 writes++;if(fail_write||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;
 memcpy(memory+a,p,n);return 0;
}
static unsigned register_calls;
static int register_error;
static struct mac_policy_conf *existing;
static int mac_policy_register(struct mac_policy_conf*c,mac_policy_handle_t*h,void*p){
 (void)p;register_calls++;if(register_error)return register_error;
 if(existing)return EEXIST;
 assert(!strcmp(c->mpc_name,"AMFI")&&c->mpc_ops->mpo_policy_syscall);
 *h=0;existing=c;return 0;
}
"""+'\n#include "'+str(provider)+'"\n'+r"""
static struct proc process={true};
static struct b2l_amfi_constraint_args args;
static void setargs(void){memcpy(memory+0x1000,&args,sizeof(args));}
static struct b2l_amfi_constraint_result result(void){struct b2l_amfi_constraint_result r;memcpy(&r,memory+0x8000,sizeof(r));return r;}
static void reset(void){
 memset(memory,0xa5,sizeof(memory));memset(&args,0,sizeof(args));
 args.constraint=0x2000;args.constraint_size=124;args.result=0x8000;args.result_size=0x404;
 /* Synthetic arbitrary DER-like bytes; no private device request fixture. */
 memset(memory+0x2000,0x30,0x4000);setargs();
 reads=writes=fail_read=0;fail_write=false;active=process.is64=true;
}
static int call(void){return b2l_amfi_constraint_syscall(&process,0x66,0x1000);}
static void assert_unavailable(void){
 struct b2l_amfi_constraint_result r=result();
 assert(r.error_code==3&&r.error_code!=0);
 const char *text="Launch constraint matching unavailable in B2L development mode; not evaluated";
 assert(!strcmp(r.description,text));
 for(size_t i=strlen(text)+1;i<sizeof(r.description);i++)assert(!r.description[i]);
 /* Shipped wrapper returns false on ENOTSUP; the libxpc invariant holds. */
 bool matched=false;assert(matched==(r.error_code==0));
}
static void test_valid_and_bounds(void){
 reset();assert(call()==ENOTSUP&&writes==1&&reads==3);assert_unavailable();
 reset();args.constraint_size=1;setargs();assert(call()==ENOTSUP&&reads==2);assert_unavailable();
 reset();args.constraint_size=0x4000;setargs();assert(call()==ENOTSUP&&reads==257);assert_unavailable();
 const unsigned bad[]={0,0x4001,UINT32_MAX};
 for(unsigned i=0;i<3;i++){reset();args.constraint_size=bad[i];setargs();assert(call()==EINVAL&&writes==1&&reads==1&&result().error_code==5);}
 const unsigned sizes[]={0,1,0x403,0x405,UINT32_MAX};
 for(unsigned i=0;i<5;i++){reset();args.result_size=sizes[i];setargs();assert(call()==EINVAL&&!writes&&reads==1);}
 reset();args.constraint=0;setargs();assert(call()==EINVAL&&writes==1&&result().error_code==5);
 reset();args.result=0;setargs();assert(call()==EINVAL&&!writes);
 /* Input and output may alias: input is copied before failure output. */
 reset();args.constraint=0x8000;setargs();assert(call()==ENOTSUP);assert_unavailable();
}
static void test_faults(void){
 reset();assert(b2l_amfi_constraint_syscall(&process,0x66,UINT64_MAX)==EFAULT&&!writes);
 reset();fail_read=1;assert(call()==EFAULT&&!writes);
 reset();fail_read=2;assert(call()==EFAULT&&writes==1&&result().error_code==5);
 reset();fail_read=3;assert(call()==EFAULT&&writes==1&&result().error_code==5);
 reset();args.constraint=UINT64_MAX-32;setargs();assert(call()==EFAULT&&writes==1&&reads==1&&result().error_code==5);
 reset();args.constraint=sizeof(memory)-1;setargs();assert(call()==EFAULT&&writes==1&&result().error_code==5);
 reset();args.result=UINT64_MAX;setargs();assert(call()==EFAULT&&writes==1);
 reset();fail_write=true;assert(call()==EFAULT&&writes==1);
 reset();args.constraint_size=0;setargs();fail_write=true;assert(call()==EFAULT&&writes==1);
}
static void test_gates_and_unknown_ops(void){
 reset();active=false;assert(call()==ENOPOLICY&&!reads&&!writes);
 reset();process.is64=false;assert(call()==ENOPOLICY&&!reads&&!writes);
 reset();assert(b2l_amfi_constraint_syscall(NULL,0x66,0x1000)==ENOPOLICY&&!reads);
 for(int op=-1;op<130;op++)if(op!=0x66){reset();assert(b2l_amfi_constraint_syscall(&process,op,UINT64_MAX)==ENOTSUP&&!reads&&!writes);}
}
static int real_callback(struct proc*p,int op,user_addr_t a){(void)p;(void)op;(void)a;return EPERM;}
static void test_registration(void){
 reset();register_calls=0;existing=NULL;b2l_amfi_constraint_registered=false;
 active=false;b2l_amfi_constraint_register();assert(!register_calls);
 active=true;register_error=ENOMEM;b2l_amfi_constraint_register();assert(register_calls==1&&!b2l_amfi_constraint_registered&&!existing);
 register_error=0;b2l_amfi_constraint_register();assert(register_calls==2&&b2l_amfi_constraint_registered&&existing==&b2l_amfi_constraint_conf);
 b2l_amfi_constraint_register();assert(register_calls==2);
 const struct mac_policy_ops real_ops={real_callback};struct mac_policy_conf real={"AMFI","real",&real_ops};
 existing=&real;b2l_amfi_constraint_registered=false;b2l_amfi_constraint_register();
 assert(!b2l_amfi_constraint_registered&&existing==&real&&existing->mpc_ops->mpo_policy_syscall(&process,0x66,0)==EPERM);
}
int main(void){
 test_valid_and_bounds();test_faults();test_gates_and_unknown_ops();test_registration();
 puts("AMFI unavailable 0x66: actual callback ABI, initialized nonmatch result, safe copies/bounds/faults, gates, unsupported ops and real-policy precedence passed");return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="b2l-amfi-constraint-") as td:
 d=pathlib.Path(td);(d/"test.c").write_text(body)
 compiler=os.getenv("CLANG") or shutil.which("clang");assert compiler
 subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer",str(d/"test.c"),"-o",str(d/"test")],check=True)
 subprocess.run([str(d/"test")],check=True)
