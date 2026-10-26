#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Test actual seed writer and boot sequencer with interrupted I/O and existing files."""
from pathlib import Path
import subprocess,tempfile,re,ast,plistlib
root=Path(__file__).resolve().parents[1]
s=(root/'xnu-overlay/bsd/kern/b2l_persona_seed.c').read_text()
constants=s[s.index('static const char manifest_path'):s.index('static bool root_type')]
xml=''.join(ast.literal_eval(x)for x in re.findall(r'"(?:[^"\\]|\\.)*"',constants[constants.index('static const char initial_manifest'):]))
assert plistlib.loads(xml.encode())==dict(UsePersonaManifestVersion=1,UsePersonaGenerationID=1,UserPersonaDictionary={})
actual=s[s.index('static int sync_seed'):s.index('static int publish_seed')]
actual+=s[s.index('void b2l_persona_before_init'):]
pre=r"""
#define B2L_PERSONA_HOST_TEST 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
enum vtype{VREG,VDIR,VLNK};
struct node{char data[1024];size_t bytes;int owner,links,type;};
typedef struct node *vnode_t;typedef int vfs_context_t;typedef char *caddr_t;
static struct node seed,root;static vnode_t rootvnode=&root;
static int active,readonly,exists,lookup_error,open_error,write_error,sync_error,read_error,close_error;
static int writes,syncs,publishes,opens,dircalls,puts_count,closes;
struct vnode_attr{off_t va_data_size;int va_uid,va_nlink;};
#define VATTR_INIT(a) memset(a,0,sizeof(*(a)))
#define VATTR_WANTED(a,b) ((void)0)
#define VATTR_IS_SUPPORTED(a,b) 1
#define UIO_READ 0
#define UIO_WRITE 1
#define UIO_SYSSPACE 0
#define IO_SYNC 1
#define MNT_WAIT 1
#define VNODE_LOOKUP_NOFOLLOW_ANY 8
#define FREAD 1
#define FWRITE 2
#define O_CREAT 0x200
#define O_NOFOLLOW 0x100
static bool root_type(vnode_t p,enum vtype type){return p&&p->type==(int)type;}
static int b2l_amfi_active(void){return active;}
static int vnode_mount(vnode_t p){(void)p;return 1;}
static int vfs_isrdonly(int m){(void)m;return readonly;}
struct stats{char f_fstypename[8];};static struct stats st={"hfs"};
static struct stats*vfs_statfs(int m){(void)m;return &st;}
static int vfs_context_current(void){return 0;}
static int vnode_lookup(const char*p,int flags,vnode_t*out,int ctx){(void)p;(void)flags;(void)ctx;if(lookup_error)return lookup_error;if(!exists)return ENOENT;*out=&seed;return 0;}
static int vnode_put(vnode_t p){(void)p;puts_count++;return 0;}
static int ensure_keybags(int c){(void)c;dircalls++;return 0;}
static int vnode_open(const char*p,int flags,int mode,int lookup,vnode_t*out,int ctx){(void)flags;(void)lookup;(void)ctx;assert(strcmp(p,staging_path)==0&&mode==0600);opens++;if(open_error)return open_error;*out=&seed;return 0;}
static int vnode_close(vnode_t p,int flags,int ctx){(void)p;(void)flags;(void)ctx;closes++;return close_error;}
static int vnode_getattr(vnode_t p,struct vnode_attr*a,int c){(void)c;a->va_data_size=(off_t)p->bytes;a->va_uid=p->owner;a->va_nlink=p->links;return 0;}
static int kauth_cred_get(void){return 0;}
static int current_proc(void){return 0;}
static int vn_rdwr(int op,vnode_t p,caddr_t buf,int n,off_t off,int space,int flags,int cred,int*resid,int proc){
 (void)space;(void)flags;(void)cred;(void)proc;assert(n>=0&&off>=0&&(size_t)(off+n)<=sizeof(p->data));*resid=0;
 if(op==UIO_READ){if(read_error)return read_error;assert((size_t)(off+n)<=p->bytes);memcpy(buf,p->data+off,n);return 0;}
 writes++;if(write_error)return write_error;memcpy(p->data+off,buf,n);p->bytes=off+n;return 0;
}
static int VNOP_FSYNC(vnode_t p,int flags,int ctx){(void)p;(void)flags;(void)ctx;syncs++;return sync_error;}
static int VFS_SYNC(int m,int flags,int ctx){(void)m;(void)flags;(void)ctx;return 0;}
static int publish_seed(vnode_t p,int ctx){(void)ctx;assert(syncs&&p->bytes==sizeof(initial_manifest)-1&&!memcmp(p->data,initial_manifest,p->bytes));publishes++;exists=1;return 0;}
"""
post=r"""
static void reset(void){memset(&seed,0,sizeof(seed));seed.links=1;seed.type=VREG;active=1;readonly=exists=lookup_error=open_error=write_error=sync_error=read_error=close_error=0;writes=syncs=publishes=opens=dircalls=puts_count=closes=0;}
int main(void){
 reset();b2l_persona_before_init();assert(writes==1&&publishes==1&&closes==1);
 b2l_persona_before_init();assert(writes==1&&publishes==1&&opens==1);
 reset();seed.bytes=37;memcpy(seed.data,initial_manifest,37);b2l_persona_before_init();assert(writes==1&&publishes==1);
 reset();seed.bytes=sizeof(initial_manifest)-1;memcpy(seed.data,initial_manifest,seed.bytes);b2l_persona_before_init();assert(!writes&&publishes==1);
 reset();seed.bytes=2;memcpy(seed.data,"XX",2);b2l_persona_before_init();assert(!writes&&!publishes&&seed.bytes==2);
 for(int i=0;i<9;i++){
  reset();switch(i){case 0:active=0;break;case 1:readonly=1;break;case 2:exists=1;break;
  case 3:lookup_error=EACCES;break;case 4:open_error=EIO;break;case 5:write_error=EIO;break;
  case 6:sync_error=EIO;break;case 7:seed.owner=501;break;case 8:seed.links=2;break;}
  b2l_persona_before_init();assert(!publishes);
 }
 reset();seed.bytes=sizeof(initial_manifest);b2l_persona_before_init();assert(!writes&&!publishes);
 puts("persona seed: plist schema, missing-only, prefix recovery, I/O failure and publication ordering PASS");
}
"""
# Constants must precede harness functions that refer to their sizes.
headers=pre[:pre.index('enum vtype')];pre=pre[pre.index('enum vtype'):]
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(headers+constants+pre+actual+post)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
