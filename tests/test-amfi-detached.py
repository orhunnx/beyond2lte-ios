#!/usr/bin/env python3
"""Exercise exact pinned data admission and the actual fcntl adapter with faults."""
from pathlib import Path
import subprocess,tempfile,struct
r=Path(__file__).resolve().parents[1]
sig=(r/'out/research-0073/device-extensions.plist.sig').read_bytes()
cd=[]
for i in range(struct.unpack_from('>I',sig,8)[0]):
 slot,at=struct.unpack_from('>II',sig,12+8*i);magic,length=struct.unpack_from('>II',sig,at)
 if magic==0xfade0c02:cd.append(sig[at:at+length])
pre=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#define MAXPATHLEN 1024
#define VREG 1
#define Z_WAITOK 1
#define UIO_READ 0
#define UIO_SYSSPACE 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define IS_64BIT_PROCESS(p) ((p)->is64)
#define os_atomic_inc_orig(p,o) (*(p))++
struct proc {bool allowed,is64;};typedef struct proc*proc_t;
struct vnode {int type;off_t length;};typedef struct vnode*vnode_t;
struct user_fsignatures {off_t fs_file_start;uint64_t fs_blob_start,fs_blob_size;};
static unsigned char contents[98192];static const char*path="/System/Library/xpc/extensions.plist";
static bool copy_fail,read_fail,short_read,alloc_fail;
static bool b2l_amfi_cache_entitled(proc_t p){return p&&p->allowed;}
static int vnode_vtype(vnode_t v){return v->type;}
static int vn_getpath(vnode_t v,char*p,int*n){(void)v;assert(*n>0&&(size_t)*n>strlen(path));strcpy(p,path);return 0;}
static int copyin(uint64_t p,void*d,size_t n){if(copy_fail)return EFAULT;memcpy(d,(void*)(uintptr_t)p,n);return 0;}
static void*vfs_context_current(void){return NULL;}
static void*kauth_cred_get(void){return NULL;}
static int vnode_size(vnode_t v,off_t*n,void*c){(void)c;*n=v->length;return 0;}
static void*kalloc_data(size_t n,int f){(void)f;return alloc_fail?NULL:malloc(n);}
static void kfree_data(void*p,size_t n){(void)n;free(p);}
static int vn_rdwr(int dir,vnode_t v,char*out,int n,off_t at,int space,int flags,void*cred,int*remaining,proc_t p){
 (void)dir;(void)v;(void)space;(void)flags;(void)cred;(void)p;
 if(read_fail)return EIO;assert(at>=0&&(uint64_t)at+n<=sizeof(contents));memcpy(out,contents+at,n);*remaining=short_read?1:0;return 0;
}
'''
post=r'''
int main(int argc,char**argv){
 assert(argc==4);FILE*f=fopen(argv[1],"rb");assert(f&&fread(contents,1,sizeof(contents),f)==sizeof(contents));fclose(f);
 struct proc p={true,true};struct vnode v={VREG,sizeof(contents)};
 for(unsigned c=2;c<4;c++){
  uint8_t blob[980];f=fopen(argv[c],"rb");assert(f);size_t n=fread(blob,1,sizeof(blob),f);fclose(f);
  struct user_fsignatures a={0,(uint64_t)(uintptr_t)blob,n};
  assert(b2l_detached_cache_map(&p,&v,&a)==0);
  /* Every bit mutation in either CodeDirectory must be rejected. */
  for(size_t i=0;i<n;i++)for(unsigned bit=0;bit<8;bit++){blob[i]^=1u<<bit;assert(b2l_detached_cache_map(&p,&v,&a)==EAUTH);blob[i]^=1u<<bit;}
  contents[127]^=1;assert(b2l_detached_cache_map(&p,&v,&a)==EAUTH);contents[127]^=1;
  p.allowed=false;assert(b2l_detached_cache_map(&p,&v,&a)==ENOTSUP);p.allowed=true;
  p.is64=false;assert(b2l_detached_cache_map(&p,&v,&a)==ENOTSUP);p.is64=true;
  path="/tmp/extensions.plist";assert(b2l_detached_cache_map(&p,&v,&a)==ENOTSUP);path="/System/Library/xpc/extensions.plist";
  v.type=2;assert(b2l_detached_cache_map(&p,&v,&a)==ENOTSUP);v.type=VREG;
  a.fs_file_start=1;assert(b2l_detached_cache_map(&p,&v,&a)==ENOTSUP);a.fs_file_start=0;
  a.fs_blob_size--;assert(b2l_detached_cache_map(&p,&v,&a)==ENOTSUP);a.fs_blob_size=n;
  v.length--;assert(b2l_detached_cache_map(&p,&v,&a)==EAUTH);v.length++;
  copy_fail=true;assert(b2l_detached_cache_map(&p,&v,&a)==EFAULT);copy_fail=false;
  read_fail=true;assert(b2l_detached_cache_map(&p,&v,&a)==EIO);read_fail=false;
  short_read=true;assert(b2l_detached_cache_map(&p,&v,&a)==EIO);short_read=false;
  alloc_fail=true;assert(b2l_detached_cache_map(&p,&v,&a)==ENOMEM);alloc_fail=false;
 }
 puts("actual detached fcntl adapter: original pair, every CD bit mutation, changed file, caller/path/size gates and copy/read/allocation failures PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-detached-') as td:
 d=Path(td);(d/'test.c').write_text(pre+'\n#include "'+str(r/'xnu-overlay/bsd/kern/b2l_amfi_detached.h')+'"\n'+post)
 for i,b in enumerate(cd):(d/f'cd{i}').write_bytes(b)
 subprocess.run(['clang','-std=c11','-D_DARWIN_C_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(r/'common/cache'),str(d/'test.c'),str(r/'common/cache/CacheSha.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test'),str(r/'out/research-0073/device-extensions.plist'),str(d/'cd0'),str(d/'cd1')],check=True)
