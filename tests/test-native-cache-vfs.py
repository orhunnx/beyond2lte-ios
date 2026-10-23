#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual undo opener against pinned XNU's native flag validation."""
import pathlib,re,subprocess,tempfile,sys
repo=pathlib.Path(__file__).resolve().parents[1]
xnu=pathlib.Path(sys.argv[1])
native=(xnu/'bsd/vfs/vfs_vnops.c').read_text()
check=re.search(r'\tif \(\(fmode & O_NOFOLLOW_ANY\) && \(fmode & O_NOFOLLOW\)\) \{\n\t\terror = EINVAL;\n\t\tgoto out;\n\t\}',native)
assert check,'native VFS flag contract changed'
source=(repo/'xnu-overlay/bsd/kern/b2l_native_cache.c').read_text()
body=source[source.index('static int undo_open(int create){'):source.index('static void undo_close(void)')]
pre=r'''
#include "b2l_nc_undo.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define FREAD 1
#define FWRITE 2
#define O_CREAT 0x200
#define O_NOFOLLOW 0x100
#define O_NOFOLLOW_ANY 0x20000000
#define VNODE_LOOKUP_NOFOLLOW_ANY 0x100
#define VREG 1
#define MNT_WAIT 1
struct vnode {int type,mount;};
typedef struct vnode *vnode_t;
typedef void *vfs_context_t;
struct vnode_attr {int va_uid,va_nlink,va_mode;int64_t va_data_size;};
#define VATTR_INIT(v) memset(v,0,sizeof(*(v)))
#define VATTR_WANTED(v,k) ((void)(v))
#define VATTR_IS_SUPPORTED(v,k) (1)
static struct vnode rootnode={VREG,1},file={VREG,1},parentnode={2,1};
static vnode_t rootvnode=&rootnode,undo_vp;
static struct b2l_nc_undo undo;
static unsigned char root_identity[12]={1,2,3},verify[B2L_NC_COPY],disk[8192];
static uint64_t length;
static int requested_flags,requested_mode,requested_create,uid,nlink=1,mode=0600,symlink;
static int open_error,attr_error,parent_error,sync_error,puts,opens;
static int native_open_flags(int fmode){int error=0;
'''
validation=check[0]+r'''
out:return error;}
'''
stubs=r'''
static vfs_context_t vfs_context_current(void){return NULL;}
static int vnode_open(const char *path,int fmode,int cmode,int flags,vnode_t*out,vfs_context_t ctx){
 (void)ctx;assert(!strcmp(path,"/private/var/db/.b2l-native-undo-v1"));
 requested_flags=flags;requested_mode=cmode;requested_create=!!(fmode&O_CREAT);
 if(flags&VNODE_LOOKUP_NOFOLLOW_ANY)fmode|=O_NOFOLLOW_ANY;
 int e=native_open_flags(fmode);if(e)return e;
 if(open_error)return open_error;if(symlink&&flags&VNODE_LOOKUP_NOFOLLOW_ANY)return ELOOP;
 *out=&file;opens++;return 0;
}
static int vnode_vtype(vnode_t v){return v->type;}
static int vnode_mount(vnode_t v){return v->mount;}
static int vnode_getattr(vnode_t v,struct vnode_attr*a,vfs_context_t ctx){(void)ctx;assert(v==&file);a->va_uid=uid;a->va_nlink=nlink;a->va_mode=mode;a->va_data_size=length;return attr_error;}
static int vnode_lookup(const char*p,int flags,vnode_t*out,vfs_context_t ctx){(void)ctx;assert(!strcmp(p,"/private/var/db")&&flags==VNODE_LOOKUP_NOFOLLOW_ANY);if(parent_error)return parent_error;*out=&parentnode;return 0;}
static int VNOP_FSYNC(vnode_t v,int flags,vfs_context_t ctx){(void)ctx;assert(v==&parentnode&&flags==MNT_WAIT);return sync_error;}
static void vnode_put(vnode_t v){assert(v==&parentnode);puts++;}
static int undo_read(void*v,uint64_t at,void*out,size_t n){assert(v==&file);if(at>length||n>length-at)return EIO;memcpy(out,disk+at,n);return 0;}
static int undo_write(void*v,uint64_t at,const void*in,size_t n){assert(v==&file);if(at>sizeof(disk)||n>sizeof(disk)-at)return EIO;memcpy(disk+at,in,n);if(at+n>length)length=at+n;return 0;}
static int undo_sync(void*v){assert(v==&file);return 0;}
'''
tests=r'''
int main(void){
 assert(native_open_flags(FREAD|FWRITE|O_NOFOLLOW|O_NOFOLLOW_ANY)==EINVAL);
 assert(!undo_open(1));assert(opens==1&&puts==1&&length==8192&&undo.count==0);
 assert(requested_flags==VNODE_LOOKUP_NOFOLLOW_ANY&&requested_mode==0600&&requested_create);
 assert(!undo_open(0)&&!requested_create);
 uid=501;assert(undo_open(1)==EINVAL);uid=0;
 nlink=2;assert(undo_open(1)==EINVAL);nlink=1;
 mode=0644;assert(undo_open(1)==EINVAL);mode=0600;
 file.mount=2;assert(undo_open(1)==EINVAL);file.mount=1;
 file.type=2;assert(undo_open(1)==EINVAL);file.type=VREG;
 symlink=1;assert(undo_open(1)==ELOOP);symlink=0;
 open_error=EACCES;assert(undo_open(1)==EACCES);open_error=0;
 parent_error=ENOENT;assert(undo_open(1)==EIO);parent_error=0;
 sync_error=EIO;assert(undo_open(1)==EIO);sync_error=0;
 assert(!undo_open(1));
 printf("Actual undo_open: pinned XNU flag compatibility, no-follow-any, root ownership/mode/mount/link checks, archive initialization and parent sync passed\n");
}
'''
# Avoid colliding with stdio puts().
src=(pre+validation+stubs+body+tests).replace('puts','parent_puts')
with tempfile.TemporaryDirectory(prefix='b2l-vfs-') as temp:
 p=pathlib.Path(temp);(p/'test.c').write_text(src)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(repo/'common/cache'),str(p/'test.c'),str(repo/'common/cache/b2l_cache_core.c'),str(repo/'common/cache/CacheSha.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
