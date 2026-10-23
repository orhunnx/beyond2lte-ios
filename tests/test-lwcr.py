#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Actual DER parser and 0x66 callback: native schema, true/false, stale identity."""
import os,pathlib,shutil,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
def der(tag,value):
 n=len(value);b=n.to_bytes((n.bit_length()+7)//8 or 1,'big');return bytes([tag])+(bytes([n]) if n<128 else bytes([0x80|len(b)])+b)+value
def integer(n):return der(2,bytes([n]))
def text(s):return der(12,s.encode())
def dictionary(items):return der(0xb0,b''.join(der(0x30,text(k)+v) for k,v in items))
def fixture(key='fixture.entitlement',op=1,extra=False):
 predicate=der(0x30,der(0x30,integer(op)+text(key)))
 ent=dictionary([('$query',predicate)])
 req=dictionary([('entitlements',ent)])
 fields=[('ccat',integer(0)),('comp',integer(1)),('reqs',req),('vers',integer(1))]
 if extra:fields.append(('extra',integer(1)))
 return der(0x70,integer(1)+dictionary(fields))
fixtures=[fixture(),fixture(op=2),fixture(extra=True),fixture('x'*255)]
arrays='\n'.join('static const uint8_t fixture%d[]={%s};'%(i,','.join(map(str,b))) for i,b in enumerate(fixtures))
body=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define ENOPOLICY 103
#define Z_WAITOK 0
typedef uint64_t user_addr_t,user64_addr_t;
typedef unsigned mac_policy_handle_t;
typedef struct {uint32_t val[8];} audit_token_t;
struct proc {bool is64;};typedef struct proc *proc_t;
struct mac_policy_ops {int (*mpo_policy_syscall)(struct proc*,int,user_addr_t);};
struct mac_policy_conf {const char *mpc_name,*mpc_fullname;const struct mac_policy_ops *mpc_ops;};
static bool active=true,found=true,present=true,noent,allocation_failure;
static unsigned references,objects,allocations,reads,writes,fail_read;
static uint32_t current_version=9;
static struct proc process={true};
static uint8_t memory[0x10000];
static bool fail_write;
static bool b2l_amfi_active(void){return active;}
static bool proc_is64bit(proc_t p){return p&&p->is64;}
static int copyin(user_addr_t a,void *p,size_t n){reads++;if(reads==fail_read||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;memcpy(p,memory+a,n);return 0;}
static int copyout(const void *p,user_addr_t a,size_t n){writes++;if(fail_write||a>sizeof(memory)||n>sizeof(memory)-a)return EFAULT;memcpy(memory+a,p,n);return 0;}
static void *kalloc_data(size_t n,int flags){(void)flags;if(allocation_failure)return NULL;allocations++;return malloc(n);}
static void kfree_data(void*p,size_t n){(void)n;assert(allocations);allocations--;free(p);}
static proc_t proc_find_audit_token(audit_token_t a){if(!found||a.val[5]!=7||a.val[7]!=9)return NULL;references++;return &process;}
static uint32_t proc_pidversion(proc_t p){assert(p==&process);return current_version;}
static void proc_rele(proc_t p){assert(p==&process&&references);references--;}
static void *proc_copy_ent(proc_t p){assert(p==&process);if(noent)return NULL;objects++;return (void*)1;}
static void *b2l_ent_copy_object(void *e,const char*k){if(!e||!present||strcmp(k,"fixture.entitlement"))return NULL;objects++;return (void*)2;}
static void b2l_ent_release(void*p){if(p){assert(objects);objects--;}}
#define strlcpy test_strlcpy
static size_t test_strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t z=l<n-1?l:n-1;memcpy(d,s,z);d[z]=0;}return l;}
static unsigned registration_calls;
static int registration_error;
static struct mac_policy_conf *existing;
static int mac_policy_register(struct mac_policy_conf*c,mac_policy_handle_t*h,void*p){
 (void)p;registration_calls++;if(registration_error)return registration_error;
 if(existing)return EEXIST;assert(!strcmp(c->mpc_name,"AMFI"));existing=c;*h=1;return 0;
}
'''+arrays+'\n#include "'+str(root/'xnu-overlay/bsd/kern/b2l_amfi_constraint.h')+'"\n'+r'''
static struct b2l_amfi_constraint_args args;
static void reset(const uint8_t *bytes,size_t n){
 assert(!references&&!objects&&!allocations);memset(memory,0xa5,sizeof(memory));memset(&args,0,sizeof(args));
 args.audit_token[5]=7;args.audit_token[7]=9;args.constraint=0x2000;args.constraint_size=(uint32_t)n;args.result=0x8000;args.result_size=0x404;
 memcpy(memory+0x2000,bytes,n);memcpy(memory+0x1000,&args,sizeof(args));
 current_version=9;active=found=present=process.is64=true;noent=allocation_failure=fail_write=false;reads=writes=fail_read=0;
}
static int call(void){int r=b2l_amfi_constraint_syscall(&process,0x66,0x1000);assert(!references&&!objects&&!allocations);return r;}
static unsigned code(void){struct b2l_amfi_constraint_result r;memcpy(&r,memory+0x8000,sizeof(r));return r.error_code;}
int main(void){
 char key[B2L_LWCR_KEY_MAX];
 assert(!b2l_lwcr_entitlement_exists(fixture0,sizeof(fixture0),key)&&!strcmp(key,"fixture.entitlement"));
 assert(!b2l_lwcr_entitlement_exists(fixture3,sizeof(fixture3),key)&&strlen(key)==255);
 /* Every truncation is rejected; no prefix becomes a successful predicate. */
 for(size_t i=0;i<sizeof(fixture0);i++)assert(b2l_lwcr_entitlement_exists(fixture0,i,key));
 uint8_t changed[sizeof(fixture0)+4];
 for(size_t i=0;i<sizeof(fixture0);i++){memcpy(changed,fixture0,sizeof(fixture0));changed[i]^=128;(void)b2l_lwcr_entitlement_exists(changed,sizeof(fixture0),key);}
 memcpy(changed,fixture0,sizeof(fixture0));changed[sizeof(fixture0)]=0;assert(b2l_lwcr_entitlement_exists(changed,sizeof(fixture0)+1,key)==EINVAL);
 const uint8_t indefinite[]={0x70,0x80,0,0},noncanonical[]={0x70,0x81,0};
 assert(b2l_lwcr_entitlement_exists(indefinite,sizeof(indefinite),key)==EINVAL);
 assert(b2l_lwcr_entitlement_exists(noncanonical,sizeof(noncanonical),key)==EINVAL);
 reset(fixture0,sizeof(fixture0));assert(!call()&&code()==0&&writes==1);
 reset(fixture0,sizeof(fixture0));present=false;assert(!call()&&code()==1);
 reset(fixture0,sizeof(fixture0));noent=true;assert(!call()&&code()==1);
 reset(fixture0,sizeof(fixture0));found=false;assert(!call()&&code()==1);
 reset(fixture0,sizeof(fixture0));current_version=10;assert(!call()&&code()==1);
 reset(fixture0,sizeof(fixture0));args.audit_token[7]++;memcpy(memory+0x1000,&args,sizeof(args));assert(!call()&&code()==1);
 reset(fixture1,sizeof(fixture1));assert(call()==ENOTSUP&&code()==3);
 reset(fixture2,sizeof(fixture2));assert(call()==ENOTSUP&&code()==3);
 reset(indefinite,sizeof(indefinite));assert(call()==EINVAL&&code()==5);
 reset(fixture0,sizeof(fixture0));fail_read=2;assert(call()==EFAULT&&code()==5);
 reset(fixture0,sizeof(fixture0));fail_write=true;assert(call()==EFAULT);
 reset(fixture0,sizeof(fixture0));allocation_failure=true;assert(call()==ENOMEM&&writes==1&&code()==5);
 reset(fixture0,sizeof(fixture0));active=false;assert(call()==ENOPOLICY&&!reads&&!writes);
 reset(fixture0,sizeof(fixture0));process.is64=false;assert(call()==ENOPOLICY&&!reads&&!writes);
 reset(fixture0,sizeof(fixture0));args.constraint=UINT64_MAX-2;memcpy(memory+0x1000,&args,sizeof(args));assert(call()==EFAULT&&code()==5);
 reset(fixture0,sizeof(fixture0));args.constraint_size=0;memcpy(memory+0x1000,&args,sizeof(args));assert(call()==EINVAL&&code()==5);
 reset(fixture0,sizeof(fixture0));args.constraint_size=0x4001;memcpy(memory+0x1000,&args,sizeof(args));assert(call()==EINVAL&&code()==5);
 reset(fixture0,sizeof(fixture0));args.constraint=0;memcpy(memory+0x1000,&args,sizeof(args));assert(call()==EINVAL&&code()==5);
 reset(fixture0,sizeof(fixture0));fail_read=1;assert(call()==EFAULT&&!writes);
 reset(fixture0,sizeof(fixture0));args.result=UINT64_MAX;memcpy(memory+0x1000,&args,sizeof(args));assert(call()==EFAULT&&writes==1);
 reset(fixture0,sizeof(fixture0));args.constraint=0x8000;memcpy(memory+0x8000,fixture0,sizeof(fixture0));memcpy(memory+0x1000,&args,sizeof(args));assert(!call()&&code()==0);
 reset(fixture0,sizeof(fixture0));args.result_size=0x403;memcpy(memory+0x1000,&args,sizeof(args));assert(call()==EINVAL&&!writes);
 reset(fixture0,sizeof(fixture0));assert(b2l_amfi_constraint_syscall(&process,0x67,0x1000)==ENOTSUP&&!reads);
 active=false;b2l_amfi_constraint_register();assert(!registration_calls);
 active=true;registration_error=ENOMEM;b2l_amfi_constraint_register();assert(!b2l_amfi_constraint_registered&&!existing);
 registration_error=0;b2l_amfi_constraint_register();assert(b2l_amfi_constraint_registered&&existing==&b2l_amfi_constraint_conf);
 unsigned calls=registration_calls;b2l_amfi_constraint_register();assert(registration_calls==calls);
 struct mac_policy_conf real={"AMFI","native",NULL};existing=&real;b2l_amfi_constraint_registered=false;
 b2l_amfi_constraint_register();assert(!b2l_amfi_constraint_registered&&existing==&real);
 puts("LWCR: native existence schema, genuine match/nonmatch, stale audit identity, metadata ownership, malformed/unknown predicates and bounded copies passed");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-lwcr-') as td:
 p=pathlib.Path(td);(p/'test.c').write_text(body)
 subprocess.run([os.getenv('CLANG') or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
