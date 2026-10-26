#!/usr/bin/env python3
"""Execute the actual one-time profile-cache rename with VFS failure injection."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
pre=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef int vfs_context_t;enum vtype{VREG,VDIR};
struct node{int type,owner,links;long bytes;};typedef struct node*vnode_t;
static struct node file={VREG,0,1,8029},parent={VDIR,0,1,0};
static vnode_t old,saved;static int renamed,syncs,fail_lookup,fail_namei,fail_sync,fail_rename;
struct componentname{int unused;};struct nameidata{vnode_t ni_vp,ni_dvp;unsigned ni_flag;const char*path;struct componentname ni_cnd;};
struct vnode_attr{unsigned va_uid,va_nlink;long va_data_size;};
#define NAMEI_NOFOLLOW_ANY 32
#define VNODE_LOOKUP_NOFOLLOW_ANY 32
#define DELETE 1
#define RENAME 2
#define OP_UNLINK 1
#define OP_RENAME 2
#define WANTPARENT 8
#define UIO_SYSSPACE 0
#undef CAST_USER_ADDR_T
#define CAST_USER_ADDR_T(p) (p)
#define MNT_WAIT 1
#define VFS_RENAME_EXCL 4
#define NDINIT(nd,a,b,c,d,p,ctx) do{(void)(ctx);memset(nd,0,sizeof(*(nd)));(nd)->path=p;}while(0)
#define VATTR_INIT(p) memset(p,0,sizeof(*(p)))
#define VATTR_WANTED(p,f) ((void)0)
#define VATTR_IS_SUPPORTED(p,f) 1
static bool root_type(vnode_t vp,enum vtype t){return vp&&vp->type==(int)t;}
static int vnode_lookup(const char*p,int flags,vnode_t*vp,int ctx){(void)ctx;assert(flags==NAMEI_NOFOLLOW_ANY&&strstr(p,"before-profile-v2"));if(fail_lookup)return fail_lookup;if(!saved)return ENOENT;*vp=saved;return 0;}
static void vnode_put(vnode_t p){assert(p);}
static int namei(struct nameidata*n){assert(n->ni_flag&NAMEI_NOFOLLOW_ANY);if(fail_namei)return fail_namei;n->ni_dvp=&parent;n->ni_vp=strstr(n->path,"before-profile-v2")?saved:old;return n->ni_vp||strstr(n->path,"before-profile-v2")?0:ENOENT;}
static void lookup_done(struct nameidata*n){assert(n->ni_dvp);}
static int vnode_getattr(vnode_t p,struct vnode_attr*v,int ctx){(void)ctx;*v=(struct vnode_attr){p->owner,p->links,p->bytes};return 0;}
static int VNOP_FSYNC(vnode_t p,int how,int ctx){(void)ctx;assert(p&&how==MNT_WAIT);syncs++;return fail_sync?EIO:0;}
static int vn_rename(vnode_t from,vnode_t*vp,struct componentname*fc,void*fa,vnode_t to,vnode_t*tv,struct componentname*tc,void*ta,unsigned flags,int ctx){(void)fc;(void)tc;(void)ctx;assert(!fa&&!ta&&from==&parent&&to==from&&flags==VFS_RENAME_EXCL&&*vp==old&&!*tv);if(fail_rename)return EIO;saved=old;old=NULL;renamed++;return 0;}
"""
post=r"""
static void reset(void){file=(struct node){VREG,0,1,8029};old=&file;saved=NULL;renamed=syncs=fail_lookup=fail_namei=fail_sync=fail_rename=0;}
int main(void){
 reset();assert(!b2l_profile_cache_refresh(0)&&!old&&saved==&file&&renamed==1&&syncs==2);
 reset();file.owner=501;assert(!b2l_profile_cache_refresh(0)&&!old&&saved==&file);
 struct node fresh={VREG,0,1,9000};old=&fresh;assert(!b2l_profile_cache_refresh(0)&&old==&fresh&&saved==&file&&renamed==1);
 for(int fault=0;fault<8;fault++){
  reset();switch(fault){case 0:file.owner=502;break;case 1:file.links=2;break;case 2:file.type=VDIR;break;case 3:file.bytes=0;break;case 4:file.bytes=4*1024*1024+1;break;case 5:fail_lookup=ELOOP;break;case 6:fail_namei=ELOOP;break;case 7:fail_sync=1;break;}
  assert(b2l_profile_cache_refresh(0)&&old==&file&&!saved&&!renamed);
 }
 reset();fail_rename=1;assert(b2l_profile_cache_refresh(0)==EIO&&old==&file&&!saved&&!renamed);
 reset();old=NULL;assert(!b2l_profile_cache_refresh(0)&&!saved&&!renamed);
 reset();saved=&parent;assert(b2l_profile_cache_refresh(0)==EINVAL&&old==&file&&!renamed);
 puts("actual profile cache refresh: one-shot/preserved-inode/metadata/symlink/I-O refusal PASS");
}
"""
with tempfile.TemporaryDirectory(prefix='b2l-profile-test-') as td:
 p=Path(td);(p/'test.c').write_text(pre+(r/'xnu-overlay/bsd/kern/b2l_profile_cache.h').read_text()+post)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
