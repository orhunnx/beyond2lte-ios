#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the prepared XNU CS_OPS_STATUS case, including native precedence."""
from pathlib import Path
import subprocess, sys, tempfile
source=(Path(sys.argv[1])/'bsd/kern/kern_proc.c').read_text()
case='case CS_OPS_STATUS: {'+source.split('\tcase CS_OPS_STATUS: {',1)[1].split('\n\tcase CS_OPS_MARKINVALID:',1)[0]
header=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#define CS_OPS_STATUS 0
#define CS_VALID 1u
#define CS_SIGNED 0x20000000u
#define CS_PLATFORM_BINARY 0x04000000u
#define CS_PLATFORM_PATH 0x08000000u
#define CS_ENFORCEMENT 0x1000u
#define CS_REQUIRE_LV 0x2000u
#define CS_FORCED_LV 0x10u
#undef USER_ADDR_NULL
#define USER_ADDR_NULL 0
struct process {uint32_t flags; bool platform, path, enforce; int pid; char p_comm[20];};
typedef struct process *proc_t;
static struct process caller={.pid=2,.p_comm="containermanagerd"};
static bool active; static int locks, metadata_error, copy_error;
static unsigned lookups, logs, copies; static proc_t expected;
static bool b2l_amfi_active(void){return active;}
static void proc_lock(proc_t p){assert(p==expected && locks==0);++locks;}
static void proc_unlock(proc_t p){assert(p==expected && locks==1);--locks;}
static unsigned proc_getcsflags(proc_t p){return p->flags;}
static bool cs_process_enforcement(proc_t p){return p->enforce;}
static bool csproc_get_platform_binary(proc_t p){return p->platform;}
static bool csproc_get_platform_path(proc_t p){return p->path;}
static int b2l_amfi_proc_copy_cdhash(proc_t p,uint8_t*hash,uint8_t*type){
 assert(locks==0 && p==expected);++lookups;
 if(metadata_error)return metadata_error;
 memset(hash,0x45,20);*type=2;return 0;
}
static proc_t current_proc(void){return &caller;}
static int proc_getpid(proc_t p){return p->pid;}
static void b2l_early_trace(const char*fmt,...){assert(locks==0&&fmt);++logs;}
static int copyout(const void*data,uintptr_t address,size_t n){
 assert(locks==0 && n==4);++copies;if(copy_error)return copy_error;memcpy((void*)address,data,n);return 0;
}
static int dispatch(proc_t pt,uintptr_t uaddr){int error=0;switch(CS_OPS_STATUS){
'''
checks=r'''
}return error;}
static uint32_t call(proc_t p){uint32_t result=0xa5a5a5a5;expected=p;unsigned raw=p->flags;
 assert(dispatch(p,(uintptr_t)&result)==0&&locks==0&&p->flags==raw);return result;}
int main(void){
 struct process child={.platform=true,.pid=10};active=true;
 uint32_t v=call(&child);assert(v==(CS_VALID|CS_PLATFORM_BINARY|CS_SIGNED)&&lookups==1);
 int failures[]={ENOENT,EINVAL,ENOTSUP,EIO,ENOMEM};
 for(unsigned i=0;i<sizeof(failures)/sizeof(failures[0]);++i){metadata_error=failures[i];assert(call(&child)==(CS_VALID|CS_PLATFORM_BINARY));}
 metadata_error=0;child.platform=false;unsigned before=lookups;
 assert(call(&child)==0&&lookups==before);
 child.platform=true;active=false;assert(call(&child)==CS_PLATFORM_BINARY&&lookups==before);
 child.flags=CS_SIGNED|CS_VALID;assert(call(&child)==(CS_SIGNED|CS_VALID|CS_PLATFORM_BINARY)&&lookups==before);
 active=true;child.flags=CS_FORCED_LV|CS_REQUIRE_LV;child.path=true;child.enforce=true;
 v=call(&child);assert(v==(CS_FORCED_LV|CS_ENFORCEMENT|CS_PLATFORM_PATH|CS_PLATFORM_BINARY|CS_VALID|CS_SIGNED));
 assert(child.flags==(CS_FORCED_LV|CS_REQUIRE_LV));
 expected=&child;before=copies;assert(dispatch(&child,USER_ADDR_NULL)==0&&copies==before&&locks==0);
 copy_error=EFAULT;assert(dispatch(&child,(uintptr_t)&v)==EFAULT&&locks==0);copy_error=0;
 for(unsigned i=0;i<30;++i)call(&child);
 assert(logs==12);strcpy(caller.p_comm,"other");call(&child);assert(logs==12);
 puts("Actual CS_OPS_STATUS: dev/platform/metadata gate, native flags, unlocked metadata, immutable process, copy faults and bounded logging PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-csstatus-') as d:
 p=Path(d);(p/'test.c').write_text(header+case+checks)
 cc=subprocess.check_output(['xcrun','--find','clang'],text=True).strip()
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run([cc,'-isysroot',sdk,'-DARM64_BOARD_CONFIG_B2L=1','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
