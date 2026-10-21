#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the actual unsigned exec boot-identity helper and dispatch branch."""
from pathlib import Path
import subprocess
import sys
import tempfile
s=(Path(sys.argv[1])/'bsd/kern/kern_exec.c').read_text()
helper='static int\nb2l_exec_unsigned_boothash('+s.split('static int\nb2l_exec_unsigned_boothash(',1)[1].split('\n#endif',1)[0]
body=s.split('} else if (!cs_process_global_enforcement()) {',1)[1].split('\n#endif',1)[0]
h=r"""
#include <CommonCrypto/CommonDigest.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#define SHA256_CTX CC_SHA256_CTX
#define SHA256_Init CC_SHA256_Init
#define SHA256_Update CC_SHA256_Update
#define SHA256_Final CC_SHA256_Final
#define SHA256_DIGEST_LENGTH CC_SHA256_DIGEST_LENGTH
#define SHA1_RESULTLEN 20
#define APP_BOOT_SESSION_KEY "executable_boothash="
#ifndef CAST_USER_ADDR_T
#define CAST_USER_ADDR_T(p) ((uintptr_t)(p))
#endif
#define UIO_SYSSPACE 1
#define FALSE 0
struct vnode_attr {uint64_t fsid,va_fileid;};
struct image_params {struct vnode_attr *ip_vattr; uint64_t ip_arch_offset; unsigned ip_applec;};
static uint32_t exe_boothash_salt=123;
static char bootsessionuuid_string[37]="00000000-0000-0000-0000-000000000001";
static unsigned adds,signed_calls;
static int add_error,enforcement;
static char captured[128];
static uint64_t vnode_get_va_fsid(struct vnode_attr*a){return a->fsid;}
static int cs_process_global_enforcement(void){return enforcement;}
static int exec_add_user_string(struct image_params*i,uintptr_t a,int u,int f){
 (void)i; assert(u==UIO_SYSSPACE && f==FALSE); ++adds;
 if(add_error)return add_error;
 assert(strlen((char*)a)<sizeof(captured)); strcpy(captured,(char*)a);return 0;
}
"""+helper+r"""
static int dispatch(struct image_params *imgp,int cdhash_error){
 int error=0;
 if(!cdhash_error){++signed_calls;return 0;}
 else if(!cs_process_global_enforcement()){
"""+body+r"""
 }
 bad:return error;
}
int main(void){
 struct vnode_attr a={41,52};struct image_params i={&a,0,7};
 enforcement=1;assert(!dispatch(&i,ENOENT)&&!adds&&i.ip_applec==7);
 enforcement=0;assert(!dispatch(&i,0)&&signed_calls==1&&!adds&&i.ip_applec==7);
 assert(!dispatch(&i,ENOENT)&&adds==1&&i.ip_applec==8);
 assert(strlen(captured)==strlen(APP_BOOT_SESSION_KEY)+40);
 assert(!strncmp(captured,APP_BOOT_SESSION_KEY,strlen(APP_BOOT_SESSION_KEY)));
 assert(strspn(captured+strlen(APP_BOOT_SESSION_KEY),"0123456789abcdef")==40);
 char first[128];strcpy(first,captured);
 assert(!dispatch(&i,ENOENT)&&!strcmp(first,captured));
 ++a.va_fileid;assert(!dispatch(&i,ENOENT)&&strcmp(first,captured));--a.va_fileid;
 ++a.fsid;assert(!dispatch(&i,ENOENT)&&strcmp(first,captured));--a.fsid;
 i.ip_arch_offset=4096;assert(!dispatch(&i,ENOENT)&&strcmp(first,captured));i.ip_arch_offset=0;
 ++exe_boothash_salt;assert(!dispatch(&i,ENOENT)&&strcmp(first,captured));--exe_boothash_salt;
 bootsessionuuid_string[35]='2';assert(!dispatch(&i,ENOENT)&&strcmp(first,captured));
 unsigned count=i.ip_applec;add_error=E2BIG;
 assert(dispatch(&i,ENOENT)==E2BIG&&i.ip_applec==count);add_error=0;
 i.ip_vattr=NULL;unsigned before=adds;assert(dispatch(&i,ENOENT)==EINVAL&&adds==before&&i.ip_applec==count);
 puts("Unsigned exec boothash: actual helper, identity/session variation, policy and error propagation passed");
}
"""
with tempfile.TemporaryDirectory(prefix='b2l-boothash-test-') as td:
 p=Path(td);(p/'test.c').write_text(h)
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run(['xcrun','clang','-isysroot',sdk,'-std=c11','-Wall','-Wextra','-Werror',
 '-Wno-deprecated-declarations','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
