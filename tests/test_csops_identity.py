#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual unsigned CS_OPS_IDENTITY helper and dispatch with copy faults."""
from pathlib import Path
import subprocess,sys,tempfile
s=(Path(sys.argv[1])/'bsd/kern/kern_proc.c').read_text()
helper='static int\nb2l_csops_copy_identity('+s.split('static int\nb2l_csops_copy_identity(',1)[1].split('\n#endif',1)[0]
branch=s.split('\tcase CS_OPS_IDENTITY:\n\tcase CS_OPS_TEAMID: {\n',1)[1].split('#endif',1)[0].split('\n',1)[1]
text=r'''
#define _DARWIN_C_SOURCE 1
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdio.h>
#define CS_OPS_IDENTITY 11
#define CS_OPS_TEAMID 14
#define CS_SIGNED 1u
struct process {unsigned flags;};typedef struct process *proc_t;
static bool enabled=true;static int copy_fault,copy_count,metadata_error;
static unsigned lookups,native_calls,allocations,releases;
static char identifier[2048]="org.example.mount";static size_t identity_length;
static proc_t observed;
static bool b2l_amfi_active(void){return enabled;}
static unsigned proc_getcsflags(proc_t p){return p->flags;}
static int b2l_amfi_proc_copy_identity(proc_t p,void**out,size_t*length){
 ++lookups;observed=p;*out=NULL;*length=0;if(metadata_error)return metadata_error;
 *length=identity_length;*out=malloc(*length);assert(*out);memcpy(*out,identifier,*length);++allocations;return 0;
}
static void kfree_data(void*p,size_t n){assert(p&&n==identity_length);free(p);++releases;}
static int copyout(const void*p,uintptr_t address,size_t n){
 ++copy_count;if(copy_count==copy_fault)return EFAULT;memcpy((void*)address,p,n);return 0;
}
'''+helper+r'''
static int dispatch(proc_t pt,int ops,size_t usize,uintptr_t uaddr){
 int error;
'''+branch+r'''
 ++native_calls;return 123;
 out:return error;
}
static unsigned char output[2048];
static int call(proc_t p,int op,size_t size){copy_count=0;memset(output,0xa5,sizeof(output));return dispatch(p,op,size,(uintptr_t)output);}
static unsigned wire_length(void){uint32_t n;memcpy(&n,output+4,4);return ntohl(n);}
int main(void){
 struct process child={0},signed_child={CS_SIGNED};identity_length=strlen(identifier)+1;
 unsigned before=lookups;
 assert(call(&child,11,7)==ERANGE&&lookups==before&&copy_count==0);
 assert(call(&child,11,8)==ERANGE&&copy_count==1&&wire_length()==8+identity_length);
 assert(output[0]==0&&output[3]==0&&output[8]==0xa5&&observed==&child);
 assert(call(&child,11,8+identity_length-1)==ERANGE&&copy_count==1);
 assert(call(&child,11,8+identity_length)==0&&copy_count==2&&!strcmp((char*)output+8,identifier));
 assert(call(&child,11,sizeof(output))==0&&output[8+identity_length]==0xa5);
 copy_fault=1;assert(call(&child,11,2048)==EFAULT&&copy_count==1&&output[0]==0xa5);
 copy_fault=2;assert(call(&child,11,2048)==EFAULT&&copy_count==2&&wire_length()==8+identity_length&&output[8]==0xa5);copy_fault=0;
 for(unsigned i=0;i<2;i++){metadata_error=i?ENOMEM:ENOENT;assert(call(&child,11,2048)==metadata_error&&copy_count==0);}metadata_error=0;
 identifier[identity_length-1]='x';assert(call(&child,11,2048)==EINVAL&&copy_count==0);identifier[identity_length-1]=0;
 identity_length=1;assert(call(&child,11,2048)==EINVAL&&copy_count==0);
 memset(identifier,'x',sizeof(identifier));identifier[1023]=0;identity_length=1024;
 assert(call(&child,11,2048)==0&&wire_length()==1032&&output[1031]==0);
 identity_length=1025;identifier[1024]=0;assert(call(&child,11,2048)==EINVAL&&copy_count==0);
 before=lookups;enabled=false;assert(call(&child,11,2048)==123&&lookups==before);enabled=true;
 assert(call(&child,14,2048)==123&&lookups==before);
 assert(call(&signed_child,11,2048)==123&&lookups==before&&native_calls==3);
 assert(allocations==releases);
 puts("Actual unsigned identity dispatch: native header/probe sizing, target, ownership, copy/metadata faults, bounds, team/signed/disabled precedence PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-identity-wire-') as d:
 p=Path(d);(p/'test.c').write_text(text)
 cc=subprocess.check_output(['xcrun','--find','clang'],text=True).strip();sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run([cc,'-isysroot',sdk,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
